// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include "WorldRender.hpp"
#include <noggit/errorHandling.h>
#include <noggit/Log.h>
#include <noggit/rendering/PointLightFlicker.hpp>
#include <external/PNG2BLP/Png2Blp.h>
#include <external/tracy/Tracy.hpp>
#include <math/frustum.hpp>
#include <noggit/application/Configuration/NoggitApplicationConfiguration.hpp>
#include <noggit/application/NoggitApplication.hpp>
#include <noggit/DBC.h>
#include <noggit/MapChunk.h>
#include <noggit/MapTile.h>
#include <noggit/TileIndex.hpp>
#include <noggit/MinimapRenderSettings.hpp>
#include <noggit/Misc.h>
#include <noggit/Model.h>
#include <noggit/ModelInstance.h>
#include <noggit/project/CurrentProject.hpp>
#include <noggit/World.h>
#include <noggit/ui/tools/ChunkManipulator/ChunkClipboard.hpp>

#include <noggit/ui/MinimapCreator.hpp>

#include <opengl/scoped.hpp>
#include <opengl/shader.hpp>
#include <opengl/types.hpp>

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QListWidget>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObjectFormat>
#include <QOpenGLFramebufferObject>
#include <QSettings>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
  struct MccvVizInstance
  {
    glm::vec4 pos;
    glm::vec4 color;
  };

  static constexpr std::size_t kMaxMccvVizInstances = 12288u;
  static constexpr float kMccvVizBallScale = 6.f;

  // Saves/restores GL_COLOR_WRITEMASK for a depth-only prepass.
  struct scoped_color_mask
  {
    GLboolean prev[4]{};

    scoped_color_mask (GLboolean r, GLboolean g, GLboolean b, GLboolean a)
    {
      gl.getBooleanv (GL_COLOR_WRITEMASK, prev);
      gl.colorMask (r, g, b, a);
    }

    ~scoped_color_mask()
    {
      gl.colorMask (prev[0], prev[1], prev[2], prev[3]);
    }

    scoped_color_mask (scoped_color_mask const&) = delete;
    scoped_color_mask& operator= (scoped_color_mask const&) = delete;
  };

  glm::vec3 map_light_forward (World::PointLight const& light)
  {
    glm::mat4 const R = glm::eulerAngleXYZ (light.rotation_radians.x
                                           , light.rotation_radians.y
                                           , light.rotation_radians.z);
    return glm::normalize (glm::vec3 (R * glm::vec4 (0.f, 0.f, -1.f, 0.f)));
  }

  float spot_effective_cone_length (World::PointLight const& light)
  {
    float const mx = std::max ({ light.spot_gizmo_scale.x, light.spot_gizmo_scale.y, light.spot_gizmo_scale.z });
    return std::max (light.spotlight_radius * mx, 0.5f);
  }

  void append_light_cone_lines (std::vector<glm::vec3>& out
                               , glm::vec3 const& apex
                               , glm::vec3 const& forward
                               , float len
                               , float outer_angle_rad
                               , int circle_segments)
  {
    if (len < 1e-3f || outer_angle_rad < 1e-4f)
      return;

    glm::vec3 const f = glm::normalize (forward);
    glm::vec3 up = std::abs (f.y) < 0.99f ? glm::vec3 (0.f, 1.f, 0.f) : glm::vec3 (1.f, 0.f, 0.f);
    glm::vec3 const right = glm::normalize (glm::cross (up, f));
    glm::vec3 const up_orth = glm::normalize (glm::cross (f, right));
    float const r = len * std::tan (outer_angle_rad);
    glm::vec3 const base_center = apex + f * len;

    out.push_back (apex);
    out.push_back (base_center);

    for (int i = 0; i < circle_segments; ++i)
    {
      float const t0 = float(i) / float(circle_segments) * glm::two_pi<float>();
      float const t1 = float(i + 1) / float(circle_segments) * glm::two_pi<float>();
      glm::vec3 const p0 = base_center + right * (std::cos (t0) * r) + up_orth * (std::sin (t0) * r);
      glm::vec3 const p1 = base_center + right * (std::cos (t1) * r) + up_orth * (std::sin (t1) * r);
      out.push_back (p0);
      out.push_back (p1);
    }

    for (int k = 0; k < 4; ++k)
    {
      float const t = float(k) / 4.f * glm::two_pi<float>();
      glm::vec3 const p = base_center + right * (std::cos (t) * r) + up_orth * (std::sin (t) * r);
      out.push_back (apex);
      out.push_back (p);
    }
  }
}

using namespace Noggit::Rendering;

WorldRender::WorldRender(World* world)
: BaseRender()
, _world(world)
, _liquid_texture_manager(world->_context)
, _view_distance(world->_settings->value("view_distance", 2000.f).toFloat() + TILE_RADIUS) // add adt radius to make sure tiles aren't culled too soon, todo: improve adt culling to prevent that from happening
, _cull_distance(0.f)
, directional_lightning(world->_settings->value("directional_lightning", true).toBool())
, local_lightning(world->_settings->value("local_lightning", true).toBool())
{
}

void WorldRender::upload()
{
  ZoneScoped;

  if (_world->mapIndex.hasAGlobalWMO())
  {
    WMOInstance inst(_world->mWmoFilename, &_world->mWmoEntry, _world->_context);
    _world->_model_instance_storage.add_wmo_instance(std::move(inst), false, false);
  }
  else
  {
    _horizon_render = std::make_unique<Noggit::map_horizon::render>(_world->horizon);
  }

  _skies = std::make_unique<Skies>(_world->mapIndex._map_id, _world->_context);
  _outdoor_lighting = std::make_unique<OutdoorLighting>();

  _buffers.upload();
  _vertex_arrays.upload();

  gl.bindBuffer(GL_UNIFORM_BUFFER, _lighting_ubo);
  gl.bufferData(GL_UNIFORM_BUFFER, sizeof(OpenGL::LightingUniformBlock), NULL, GL_DYNAMIC_DRAW);
  gl.bindBufferRange(GL_UNIFORM_BUFFER, OpenGL::ubo_targets::LIGHTING, _lighting_ubo, 0, sizeof(OpenGL::LightingUniformBlock));
  gl.bindBuffer(GL_UNIFORM_BUFFER, 0);

  gl.bindBuffer(GL_UNIFORM_BUFFER, _point_lights_ubo);
  gl.bufferData(GL_UNIFORM_BUFFER, sizeof(OpenGL::PointLightsUniformBlock), NULL, GL_DYNAMIC_DRAW);
  gl.bindBufferRange(GL_UNIFORM_BUFFER, OpenGL::ubo_targets::POINT_LIGHTS, _point_lights_ubo, 0, sizeof(OpenGL::PointLightsUniformBlock));
  gl.bindBuffer(GL_UNIFORM_BUFFER, 0);

  _mcnk_program.reset(
    new OpenGL::program{
      { GL_VERTEX_SHADER, OpenGL::shader::src_from_qrc("terrain_vs") },
      { GL_FRAGMENT_SHADER, OpenGL::shader::src_from_qrc("terrain_fs") },
    });

  {
    OpenGL::Scoped::use_program mcnk_shader {*_mcnk_program.get()};

    setupChunkBuffers();
    setupChunkVAO(mcnk_shader);

    mcnk_shader.bind_uniform_block("lighting", 1);
    mcnk_shader.bind_uniform_block("overlay_params", 2);
    mcnk_shader.bind_uniform_block("chunk_instances", 3);
    mcnk_shader.bind_uniform_block("point_lights", 5);

    gl.bindBuffer(GL_UNIFORM_BUFFER, _terrain_params_ubo);
    gl.bufferData(GL_UNIFORM_BUFFER, sizeof(OpenGL::TerrainParamsUniformBlock), NULL, GL_STATIC_DRAW);
    gl.bindBufferRange(GL_UNIFORM_BUFFER, OpenGL::ubo_targets::TERRAIN_OVERLAYS, _terrain_params_ubo, 0, sizeof(OpenGL::TerrainParamsUniformBlock));
    gl.bindBuffer(GL_UNIFORM_BUFFER, 0);

    mcnk_shader.uniform("heightmap", 0);
    mcnk_shader.uniform("mccv", 1);
    mcnk_shader.uniform("shadowmap", 2);
    mcnk_shader.uniform("alphamap", 3);
    mcnk_shader.uniform("stamp_brush", 4);
    mcnk_shader.uniform("base_instance", 0);

    std::vector<int> samplers {5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    mcnk_shader.uniform("textures", samplers);
  }

  _m2_program.reset(
    new OpenGL::program{
      { GL_VERTEX_SHADER, OpenGL::shader::src_from_qrc("m2_vs") },
      { GL_FRAGMENT_SHADER, OpenGL::shader::src_from_qrc("m2_fs") },
    });

  _m2_instanced_program.reset(
    new OpenGL::program{
      { GL_VERTEX_SHADER, OpenGL::shader::src_from_qrc("m2_vs", {"instanced"}) },
      { GL_FRAGMENT_SHADER, OpenGL::shader::src_from_qrc("m2_fs") },
    });

  _m2_box_program.reset(
    new OpenGL::program{
      { GL_VERTEX_SHADER, OpenGL::shader::src_from_qrc("m2_box_vs") },
      { GL_FRAGMENT_SHADER, OpenGL::shader::src_from_qrc("m2_box_fs") },
    });

  _m2_ribbons_program.reset(
    new OpenGL::program{
      { GL_VERTEX_SHADER, OpenGL::shader::src_from_qrc("ribbon_vs") },
      { GL_FRAGMENT_SHADER, OpenGL::shader::src_from_qrc("ribbon_fs") },
    });

  _m2_particles_program.reset(
    new OpenGL::program{
      { GL_VERTEX_SHADER, OpenGL::shader::src_from_qrc("particle_vs") },
      { GL_FRAGMENT_SHADER, OpenGL::shader::src_from_qrc("particle_fs") },
    });

  _mfbo_program.reset(
    new OpenGL::program{
      { GL_VERTEX_SHADER, OpenGL::shader::src_from_qrc("mfbo_vs") },
      { GL_FRAGMENT_SHADER, OpenGL::shader::src_from_qrc("mfbo_fs") },
    });

  _wmo_program.reset(
    new OpenGL::program{
      { GL_VERTEX_SHADER, OpenGL::shader::src_from_qrc("wmo_vs") },
      { GL_FRAGMENT_SHADER, OpenGL::shader::src_from_qrc("wmo_fs") },
    });

  _liquid_program.reset(
    new OpenGL::program{
      { GL_VERTEX_SHADER, OpenGL::shader::src_from_qrc("liquid_vs") },
      { GL_FRAGMENT_SHADER, OpenGL::shader::src_from_qrc("liquid_fs") },
    });

  _occluder_program.reset(
    new OpenGL::program{
      { GL_VERTEX_SHADER, OpenGL::shader::src_from_qrc("occluder_vs") },
      { GL_FRAGMENT_SHADER, OpenGL::shader::src_from_qrc("occluder_fs") },
    });

  _liquid_texture_manager.upload();

  setupOccluderBuffers();

  {
    OpenGL::Scoped::use_program m2_shader {*_m2_program.get()};
    m2_shader.uniform("bone_matrices", 0);
    m2_shader.uniform("tex1", 1);
    m2_shader.uniform("tex2", 2);
    m2_shader.uniform("terrain_uv_mask", 0);
    m2_shader.bind_uniform_block("lighting", 1);
    m2_shader.bind_uniform_block("point_lights", 5);
  }

  {
    std::vector<int> samplers {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    OpenGL::Scoped::use_program wmo_program {*_wmo_program.get()};
    wmo_program.uniform("render_batches_tex", 0);
    wmo_program.uniform("texture_samplers", samplers);
    wmo_program.bind_uniform_block("lighting", 1);
    wmo_program.bind_uniform_block("point_lights", 5);
  }

  {
    OpenGL::Scoped::use_program m2_shader_instanced {*_m2_instanced_program.get()};
    m2_shader_instanced.bind_uniform_block("lighting", 1);
    m2_shader_instanced.bind_uniform_block("point_lights", 5);
    m2_shader_instanced.uniform("bone_matrices", 0);
    m2_shader_instanced.uniform("tex1", 1);
    m2_shader_instanced.uniform("tex2", 2);
    m2_shader_instanced.uniform("terrain_uv_mask", 0);
  }

  {
    OpenGL::Scoped::use_program liquid_render {*_liquid_program.get()};

    setupLiquidChunkBuffers();
    setupLiquidChunkVAO(liquid_render);

    static std::vector<int> liquid_samplers {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

    liquid_render.bind_uniform_block("lighting", 1);
    liquid_render.bind_uniform_block("liquid_layers_params", 4);
    liquid_render.uniform("vertex_data", 0);
    liquid_render.uniform("texture_samplers", liquid_samplers);
  }

  setupMccvVizBuffers();
}

void WorldRender::draw (glm::mat4x4 const& model_view
    , glm::mat4x4 const& projection
    , glm::vec3 const& cursor_pos
    , glm::vec4 const& cursor_color
    , glm::vec3 const& ref_pos
    , glm::vec3 const& camera_pos
    , MinimapRenderSettings* minimap_render_settings
    , WorldRenderParams const& render_settings
)
{

  ZoneScoped;

  glm::mat4x4 const mvp(projection * model_view);
  math::frustum const frustum (mvp);

  if (render_settings.camera_moved)
    updateMVPUniformBlock(model_view, projection);

  gl.disable(GL_DEPTH_TEST);

  if (!render_settings.minimap_render)
  {
    int daytime = static_cast<int>(_world->time) % 2880;
    // always render local lights in sky/lightning editing mode.
    bool render_local_lightning = render_settings.editing_mode == editing_mode::light ? true : local_lightning;
    _skies->update_sky_colors(camera_pos, daytime, !render_local_lightning);
    updateLightingUniformBlock(render_settings.draw_fog, camera_pos);
    updatePointLightsUniformBlock(render_settings.draw_point_lights, camera_pos);
  }
  else
  {
    updateLightingUniformBlockMinimap(minimap_render_settings);
  }

  // setup render settings for minimap
  if (render_settings.minimap_render)
  {
    _terrain_params_ubo_data.draw_shadows = minimap_render_settings->draw_shadows;
    _terrain_params_ubo_data.draw_lines = minimap_render_settings->draw_adt_grid;
    _terrain_params_ubo_data.draw_terrain_height_contour = minimap_render_settings->draw_elevation;
    _terrain_params_ubo_data.draw_hole_lines = false;
    _terrain_params_ubo_data.draw_impass_overlay = false;
    _terrain_params_ubo_data.draw_areaid_overlay = false;
    _terrain_params_ubo_data.draw_paintability_overlay = false;
    _terrain_params_ubo_data.draw_selection_overlay = false;
    _terrain_params_ubo_data.draw_wireframe = false;
    _terrain_params_ubo_data.draw_groundeffectid_overlay = false;
    _terrain_params_ubo_data.draw_groundeffect_layerid_overlay = false;
    _terrain_params_ubo_data.draw_noeffectdoodad_overlay = false;
    _terrain_params_ubo_data.draw_only_normals = minimap_render_settings->draw_only_normals;
    _terrain_params_ubo_data.point_normals_up = minimap_render_settings->point_normals_up;
    _terrain_params_ubo_data.draw_texture_layer_count_overlay = false;
    _terrain_params_ubo_data.draw_tileset = true;
    _need_terrain_params_ubo_update = true;
  }

  // After coming out of minimap rendering mode and draw_only_normals is still on, disable it.
  if (!render_settings.minimap_render && _terrain_params_ubo_data.draw_only_normals) {
      _terrain_params_ubo_data.draw_only_normals = false;
      _need_terrain_params_ubo_update = true;
  }

  // After coming out of minimap rendering mode and point_normals_up is still on, disable it.
  if (!render_settings.minimap_render && _terrain_params_ubo_data.point_normals_up) {
      _terrain_params_ubo_data.point_normals_up = false;
      _need_terrain_params_ubo_update = true;
  }

  if (_need_terrain_params_ubo_update)
    updateTerrainParamsUniformBlock();

  // Chunk Manipulator: use the terrain selection overlay to tint selected chunks, and show paste footprint preview.
  std::map<TileIndex, std::array<std::uint8_t, 256>> chunk_manip_sel_masks;
  std::map<TileIndex, std::array<std::uint8_t, 256>> chunk_manip_prev_masks;
  std::map<TileIndex, std::vector<std::pair<int, std::vector<float>>>> chunk_manip_prev_height_rows;
  bool chunk_manip_has_any_overlay = false;
  if (render_settings.draw_chunk_manipulator_selection && !render_settings.minimap_render
      && render_settings.display_mode == display_mode::in_3D)
  {
    if (auto* clip = _world->chunkClipboard())
    {
      auto const& sel = clip->selectedChunks();
      for (auto const& idx : sel)
      {
        if (!idx.tile_index.is_valid() || idx.x >= 16 || idx.z >= 16)
          continue;
        auto& m = chunk_manip_sel_masks[idx.tile_index];
        m[idx.x * 16u + idx.z] = 1u;
        chunk_manip_has_any_overlay = true;
      }

      if (clip->hasCachedCopy())
      {
        MapChunk* pivot_chunk = _world->getChunkAt(cursor_pos);
        if (pivot_chunk)
        {
          TileIndex const pivot_ti = pivot_chunk->mt->index;
          int const pivot_gx = static_cast<int>(pivot_ti.x) * 16 + pivot_chunk->px;
          int const pivot_gz = static_cast<int>(pivot_ti.z) * 16 + pivot_chunk->py;

          for (auto const& entry : clip->cachedChunks())
          {
            auto const& rel = entry.first;
            auto const& cache = entry.second;
            int const tgx = pivot_gx + rel.rel_x;
            int const tgz = pivot_gz + rel.rel_z;
            if (tgx < 0 || tgz < 0)
              continue;
            std::size_t const tile_x = static_cast<std::size_t>(tgx / 16);
            std::size_t const tile_z = static_cast<std::size_t>(tgz / 16);
            TileIndex const ti{ tile_x, tile_z };
            if (!ti.is_valid())
              continue;
            unsigned const cx = static_cast<unsigned>(tgx % 16);
            unsigned const cz = static_cast<unsigned>(tgz % 16);
            if (cx >= 16 || cz >= 16)
              continue;
            auto& m = chunk_manip_prev_masks[ti];
            m[cx * 16u + cz] = 1u;
            chunk_manip_has_any_overlay = true;

            if (cache.terrain_height)
            {
              // Build a heightmap row (RGBA32F) for this chunk instance:
              // normal=(0,1,0), height=vertex.y from cached mVertices.
              std::vector<float> row(static_cast<std::size_t>(mapbufsize) * 4u);
              float const* src = reinterpret_cast<float const*>(cache.terrain_height->data());
              float const paste_dy = clip->pasteTerrainHeightOffset();
              for (int v = 0; v < mapbufsize; ++v)
              {
                float const h = src[v * 3 + 1] + paste_dy;
                row[static_cast<std::size_t>(v) * 4 + 0] = 0.f;
                row[static_cast<std::size_t>(v) * 4 + 1] = 1.f;
                row[static_cast<std::size_t>(v) * 4 + 2] = 0.f;
                row[static_cast<std::size_t>(v) * 4 + 3] = h;
              }
              chunk_manip_prev_height_rows[ti].emplace_back(static_cast<int>(cx * 16u + cz), std::move(row));
            }
          }
        }
      }
    }
  }

  // Enable the shader overlay only when the tool requests it.
  if (_terrain_params_ubo_data.draw_selection_overlay != (render_settings.draw_chunk_manipulator_selection ? 1 : 0))
  {
    _terrain_params_ubo_data.draw_selection_overlay = render_settings.draw_chunk_manipulator_selection ? 1 : 0;
    _need_terrain_params_ubo_update = true;
    updateTerrainParamsUniformBlock();
  }

  // Frustum culling
  _world->_n_loaded_tiles = 0;
  unsigned tile_counter = 0;

  bool modern_features = Noggit::Application::NoggitApplication::instance()->getConfiguration()->modern_features;

  for (MapTile* tile : _world->mapIndex.loaded_tiles())
  {
    tile->_was_rendered_last_frame = false;

    if (render_settings.minimap_render)
    {
      tile->calcCamDist(camera_pos);
      tile->renderer()->setFrustumCulled(false);
      tile->renderer()->setObjectsFrustumCullTest(2);
      tile->renderer()->setOccluded(false);
      _world->_loaded_tiles_buffer[tile_counter] = std::make_pair(std::make_pair(static_cast<int>(tile->index.x), static_cast<int>(tile->index.z)), tile);

      tile_counter++;
      _world->_n_loaded_tiles++;
      continue;
    }

    auto const tile_extents = tile->getTerrainWaterCullExtents();
    if (frustum.intersects(tile_extents[1], tile_extents[0]) || tile->getChunkUpdateFlags())
    {
      tile->calcCamDist(camera_pos);
      _world->_loaded_tiles_buffer[tile_counter] = std::make_pair(std::make_pair(static_cast<int>(tile->index.x), static_cast<int>(tile->index.z)), tile);

      tile->renderer()->setObjectsFrustumCullTest(1);
      if (frustum.contains(tile_extents[0]) && frustum.contains(tile_extents[1]))
      {
        tile->renderer()->setObjectsFrustumCullTest( tile->renderer()->objectsFrustumCullTest() + 1);
      }

      if (tile->renderer()->isFrustumCulled())
      {
        tile->renderer()->setOverrideOcclusionCulling(true);
        tile->renderer()->discardTileOcclusionQuery();
        tile->renderer()->setOccluded(false);
      }

      tile->renderer()->setFrustumCulled(false);

      tile_counter++;
    }
    else
    {
      tile->renderer()->setFrustumCulled(true);
      tile->renderer()->setObjectsFrustumCullTest(0);
    }

    _world->_n_loaded_tiles++;
  }

  auto buf_end = _world->_loaded_tiles_buffer.begin() + tile_counter;
  _world->_loaded_tiles_buffer[tile_counter] = std::make_pair<std::pair<int, int>, MapTile*>(std::make_pair<int, int>(0, 0), nullptr);


  // It is always import to sort tiles __front to back__.
  // Otherwise selection would not work. Overdraw overhead is gonna occur as well.
  // TODO: perhaps parallel sort?
  std::sort(_world->_loaded_tiles_buffer.begin(), buf_end,
            [](std::pair<std::pair<int, int>, MapTile*>& a, std::pair<std::pair<int, int>, MapTile*>& b) -> bool
            {
              if (!a.second)
              {
                return false;
              }

              if (!b.second)
              {
                return true;
              }

              return a.second->camDist() < b.second->camDist();
            });

  // only draw the sky in 3D (requires m2 shader program)
  if(!render_settings.minimap_render && render_settings.display_mode == display_mode::in_3D && render_settings.draw_sky
     && _m2_program)
  {
    ZoneScopedN("World::draw() : Draw skies");
    OpenGL::Scoped::use_program m2_shader {*_m2_program.get()};
    m2_shader.uniform("model_view", model_view);
    m2_shader.uniform("projection", projection);

    bool hadSky = false;

    if (render_settings.draw_skybox && (render_settings.draw_wmo || _world->mapIndex.hasAGlobalWMO()))
    {
      _world->_model_instance_storage.for_each_wmo_instance
          (
              [&] (WMOInstance& wmo)
              {
                if (wmo.wmo->finishedLoading() && wmo.wmo->skybox)
                {
                  if (wmo.getGroupExtents().empty())
                  {
                    wmo.recalcExtents();
                  }

                  hadSky = wmo.wmo->renderer()->drawSkybox(model_view
                      , camera_pos
                      , m2_shader
                      , frustum
                      , _cull_distance
                      , _world->animtime
                      , render_settings.draw_model_animations
                      , wmo.getExtents()[0]
                      , wmo.getExtents()[1]
                      , wmo.getGroupExtents()
                  );
                }

              }
              , [&] () { return hadSky; }
          );
    }

    if (!hadSky)
    {
      _skies->draw( model_view
          , projection
          , camera_pos
          , m2_shader
          , frustum
          , _cull_distance
          , _world->animtime
          , _world->time
          , render_settings.draw_skybox
          , _outdoor_light_stats
      );
    }
  }

  _cull_distance= render_settings.draw_fog ? _skies->fog_distance_end() : _view_distance;

  // Draw verylowres heightmap
  if (!_world->mapIndex.hasAGlobalWMO() && render_settings.draw_fog && render_settings.draw_terrain)
  {
    ZoneScopedN("World::draw() : Draw horizon");
    _horizon_render->draw (model_view, projection, 
      &_world->mapIndex, _skies->color_set[SKY_FOG_COLOR],
      _cull_distance,
      frustum,
      camera_pos,
      render_settings.display_mode);
  }

  gl.enable(GL_DEPTH_TEST);
  gl.depthFunc(GL_LEQUAL); // less z-fighting artifacts this way, I think
  //gl.disable(GL_BLEND);
  gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  //gl.disable(GL_CULL_FACE);

  _world->_n_rendered_tiles = 0;
  _world->_n_rendered_objects = 0;

  if (render_settings.draw_terrain && _mcnk_program)
  {
    ZoneScopedN("World::draw() : Draw terrain");

    gl.disable(GL_BLEND);

    {
      OpenGL::Scoped::use_program mcnk_shader{ *_mcnk_program.get() };
      mcnk_shader.uniform("model_view", model_view);
      mcnk_shader.uniform("projection", projection);

      mcnk_shader.uniform("enable_mists_heightmapping", modern_features);
      mcnk_shader.uniform("albedo_only", 0);
      mcnk_shader.uniform("preview_pass", 0);
      mcnk_shader.uniform("preview_alpha", 1.0f);
      mcnk_shader.uniform("camera", glm::vec3(camera_pos.x, camera_pos.y, camera_pos.z));
      mcnk_shader.uniform("animtime", static_cast<int>(_world->animtime));

      if (render_settings.cursor_type != CursorType::NONE)
      {
        mcnk_shader.uniform("draw_cursor_circle", static_cast<int>(render_settings.cursor_type));
        mcnk_shader.uniform("cursor_position", glm::vec3(cursor_pos.x, cursor_pos.y, cursor_pos.z));
        mcnk_shader.uniform("cursorRotation", render_settings.cursorRotation);
        mcnk_shader.uniform("outer_cursor_radius", render_settings.brush_radius);
        mcnk_shader.uniform("inner_cursor_ratio", render_settings.inner_radius_ratio);
        mcnk_shader.uniform("cursor_color", cursor_color);
      }
      else
      {
        mcnk_shader.uniform("draw_cursor_circle", 0);
      }

      gl.bindVertexArray(_mapchunk_vao);
      gl.bindBuffer(GL_ELEMENT_ARRAY_BUFFER, _mapchunk_index);

      int num_chunks_uploaded_alphamap = 0;

      for (auto const& pair : _world->_loaded_tiles_buffer)
      {
        MapTile* tile = pair.second;

        if (!tile)
        {
          break;
        }

        if (render_settings.minimap_render)
          tile->renderer()->setOccluded(false);

        if (tile->renderer()->isOccluded() && !tile->getChunkUpdateFlags() && !tile->renderer()->isOverridingOcclusionCulling())
          continue;

        // skipping unfinished adts really improves performance so we don't have to reuplaod them every frame
        if (!tile->texturesFinishedLoading())
          continue;

        // Limit rate uploading alphamap data to avoid long frame times (causes freezes)
        // TODO make it dynamic based on target frame time and last frame times
        bool skip_updates = false;
        if (num_chunks_uploaded_alphamap > _frame_max_chunk_updates)
          skip_updates = true;

        if (render_settings.draw_chunk_manipulator_selection && !render_settings.minimap_render
            && render_settings.display_mode == display_mode::in_3D)
        {
          auto const it_sel = chunk_manip_sel_masks.find(tile->index);
          auto const it_prev = chunk_manip_prev_masks.find(tile->index);
          if (it_sel != chunk_manip_sel_masks.end() || it_prev != chunk_manip_prev_masks.end())
          {
            tile->renderer()->setChunkManipulatorOverlays(
              it_sel != chunk_manip_sel_masks.end() ? it_sel->second : std::array<std::uint8_t, 256>{},
              it_prev != chunk_manip_prev_masks.end() ? it_prev->second : std::array<std::uint8_t, 256>{});
          }
          else
          {
            tile->renderer()->clearChunkManipulatorOverlays();
          }
        }
        else
        {
          tile->renderer()->clearChunkManipulatorOverlays();
        }

        tile->renderer()->draw(
            mcnk_shader
            , camera_pos
            , render_settings.show_unpaintable_chunks
            , render_settings.draw_paintability_overlay
            , render_settings.editing_mode == editing_mode::minimap
              && minimap_render_settings->selected_tiles.at(64 * tile->index.x + tile->index.z)
            , skip_updates
        );

        // Chunk Manipulator live paste preview: draw ghost terrain using cached height rows (50% opacity).
        if (render_settings.draw_chunk_manipulator_selection && !render_settings.minimap_render
            && render_settings.display_mode == display_mode::in_3D)
        {
          auto it_rows = chunk_manip_prev_height_rows.find(tile->index);
          if (it_rows != chunk_manip_prev_height_rows.end() && !it_rows->second.empty())
          {
            OpenGL::Scoped::bool_setter<GL_BLEND, GL_TRUE> const blend_on;
            gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            tile->renderer()->updateChunkManipulatorPreviewHeightmap(it_rows->second);
            tile->renderer()->drawChunkManipulatorPreview(mcnk_shader, 0.5f);
          }
        }

        num_chunks_uploaded_alphamap += tile->renderer()->numUploadedChunkAlphamaps();

        // if (tile->renderer()->alphamapUploadedLastFrame())
        //   num_tiles_uploaded_alphamap++;

        _world->_n_rendered_tiles++;
        tile->_was_rendered_last_frame = true;

      }

      gl.bindVertexArray(0);
      gl.bindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    drawMccvVertexAltViz(model_view, projection, mvp, camera_pos, cursor_color, render_settings);
    drawRampPreview(mvp, render_settings);
  }

  // Terrain base-color lookup texture for WMO shader-16 blending (modern clients).
  // Render top-down albedo into an FBO around the camera, then sample it in the WMO shader.
  {
    QSettings settings;
    bool const wmo_terrain_blend = settings.value("wmo_terrain_blend", modern_features).toBool();

    if (wmo_terrain_blend && modern_features && render_settings.draw_terrain && render_settings.draw_wmo && _mcnk_program)
    {
      glm::vec2 const center_xz(camera_pos.x, camera_pos.z);
      float const move_thresh = _terrain_blend_world_size * 0.25f;
      bool const moved =
        glm::distance(center_xz, _terrain_blend_last_center_xz) > move_thresh || _terrain_blend_fbo == nullptr;

      if (moved)
      {
        _terrain_blend_last_center_xz = center_xz;
        _terrain_blend_origin_xz = center_xz - glm::vec2(_terrain_blend_world_size * 0.5f);
        _terrain_blend_inv_size = (_terrain_blend_world_size > 1e-5f) ? (1.f / _terrain_blend_world_size) : 0.f;

        if (!_terrain_blend_fbo)
        {
          QOpenGLFramebufferObjectFormat fmt;
          fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
          fmt.setInternalTextureFormat(GL_RGBA8);
          _terrain_blend_fbo = std::make_unique<QOpenGLFramebufferObject>(_terrain_blend_tex_size, _terrain_blend_tex_size, fmt);
          _terrain_blend_color_tex = _terrain_blend_fbo->texture();
        }

        GLint prev_viewport[4]{};
        gl.getIntegerv(GL_VIEWPORT, prev_viewport);

        _terrain_blend_fbo->bind();
        gl.viewport(0, 0, _terrain_blend_tex_size, _terrain_blend_tex_size);
        gl.disable(GL_BLEND);
        gl.enable(GL_DEPTH_TEST);
        gl.depthMask(GL_TRUE);
        gl.clearColor(0.f, 0.f, 0.f, 1.f);
        gl.clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Top-down ortho covering [_terrain_blend_origin_xz, origin+world_size].
        glm::vec3 const eye(center_xz.x, camera_pos.y + 4096.f, center_xz.y);
        glm::vec3 const target(center_xz.x, 0.f, center_xz.y);
        glm::mat4 const bake_view = glm::lookAt(eye, target, glm::vec3(0.f, 0.f, -1.f));
        float const half = _terrain_blend_world_size * 0.5f;
        glm::mat4 const bake_proj = glm::ortho(-half, half, -half, half, 1.f, 16384.f);

        OpenGL::Scoped::use_program mcnk_shader{ *_mcnk_program.get() };
        mcnk_shader.uniform("model_view", bake_view);
        mcnk_shader.uniform("projection", bake_proj);
        mcnk_shader.uniform("enable_mists_heightmapping", modern_features);
        mcnk_shader.uniform("albedo_only", 1);
        mcnk_shader.uniform("preview_pass", 0);
        mcnk_shader.uniform("preview_alpha", 1.0f);
        mcnk_shader.uniform("draw_cursor_circle", 0);
        mcnk_shader.uniform("camera", glm::vec3(eye.x, eye.y, eye.z));
        mcnk_shader.uniform("animtime", static_cast<int>(_world->animtime));

        gl.bindVertexArray(_mapchunk_vao);
        gl.bindBuffer(GL_ELEMENT_ARRAY_BUFFER, _mapchunk_index);

        for (auto const& pair : _world->_loaded_tiles_buffer)
        {
          MapTile* tile = pair.second;
          if (!tile)
            break;
          if (!tile->texturesFinishedLoading())
            continue;
          tile->renderer()->setOccluded(false);
          tile->renderer()->draw(mcnk_shader, glm::vec3(center_xz.x, camera_pos.y, center_xz.y),
                                 /*show_unpaintable_chunks*/ false,
                                 /*draw_paintability_overlay*/ false,
                                 /*is_selected*/ false,
                                 /*skip_upload_alphamap*/ false);
        }

        gl.bindVertexArray(0);
        gl.bindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

        _terrain_blend_fbo->release();
        gl.viewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
      }
    }
  }

  drawChunkManipulatorSelection(model_view, projection, render_settings);

  if (render_settings.editing_mode == editing_mode::object && _world->has_multiple_model_selected())
  {
    ZoneScopedN("World::draw() : Draw pivot point");
    if (_world->_multi_select_pivot.has_value())
    {
      OpenGL::Scoped::bool_setter<GL_DEPTH_TEST, GL_FALSE> const disable_depth_test;

      float dist = glm::distance(camera_pos, _world->_multi_select_pivot.value());
      _sphere_render.draw(mvp, _world->_multi_select_pivot.value(), cursor_color, std::min(2.f, std::max(0.15f, dist * 0.02f)));
    }
    else
    {
      // assert(false);
    }
  }

  if (render_settings.use_ref_pos)
  {
    ZoneScopedN("World::draw() : Draw ref pos");
    _sphere_render.draw(mvp, ref_pos, cursor_color, 0.3f);
  }

  if (render_settings.editing_mode == editing_mode::ground && render_settings.ground_editing_brush == eTerrainType_Vertex)
  {
    ZoneScopedN("World::draw() : Draw vertex points");
    float size = glm::distance(_world->vertexCenter(), camera_pos);
    gl.pointSize(std::max(0.001f, 10.0f - (1.25f * size / CHUNKSIZE)));

    for (glm::vec3 const* pos : _world->_vertices_selected)
    {
      _sphere_render.draw(mvp, *pos, glm::vec4(1.f, 0.f, 0.f, 1.f), 0.5f);
    }

    _sphere_render.draw(mvp, _world->vertexCenter(), cursor_color, 2.f);
  }

  std::unordered_map<Model*, std::size_t> model_with_particles;

  tsl::robin_map<Model*, std::vector<glm::mat4x4>> models_to_draw;
  std::vector<WMOInstance*> wmos_to_draw;
  std::unordered_map<Model*, std::size_t> model_boxes_to_draw;

  // frame counter loop. pretty hacky but works
  // this is used to make sure no object is processed more than once within a frame
  static int frame = 0;

  if (frame == std::numeric_limits<int>::max())
  {
    frame = 0;
  }
  else
  {
    frame++;
  }

  for (auto const& pair : _world->_loaded_tiles_buffer)
  {
    MapTile* tile = pair.second;

    if (!tile)
    {
      break;
    }

    if (render_settings.minimap_render)
      tile->renderer()->setOccluded(false);

    if (tile->renderer()->isOccluded() && !tile->getChunkUpdateFlags() && !tile->renderer()->isOverridingOcclusionCulling())
      continue;

    // early dist check
    // TODO: optional
    if (tile->camDist() > _cull_distance)
      continue;


    // TODO: subject to potential generalization
    for (auto& pair : tile->getObjectInstances())
    {
      if (!pair.first->finishedLoading())
        continue;

      if (pair.second[0]->which() == eMODEL)
      {
        if (!render_settings.draw_models && !(render_settings.minimap_render && minimap_render_settings->use_filters))
        {
          // can optimize this with a tile.rendered_m2s_lastframe or just check if models are enabled
          for (auto& instance : pair.second)
          {
            instance->_rendered_last_frame = false;
          }
          continue;
        }

        auto& instances = models_to_draw[reinterpret_cast<Model*>(pair.first)];

        // memory allocation heuristic. all objects will pass if tile is entirely in frustum.
        // otherwise we only allocate for a half

        if (tile->renderer()->objectsFrustumCullTest() > 1)
        {
          instances.reserve(instances.size() + pair.second.size());
        }
        else
        {
          instances.reserve(instances.size() + pair.second.size() / 2);
        }


        for (auto& instance : pair.second)
        {
          instance->_rendered_last_frame = false;

          // do not render twice the cross-referenced objects twice
          if (instance->frame == frame)
          {
            instance->_rendered_last_frame = true;
            continue;
          }

          auto m2_instance = static_cast<ModelInstance*>(instance);

          if (!render_settings.draw_hidden_models && m2_instance->model->is_hidden())
            continue;

          instance->frame = frame;

          bool render = false;
          // experimental : if camera and object haven't moved/changed since last frame, we don't need to do frustum culling again
          if (!render_settings.camera_moved && !m2_instance->extentsDirty()/* && not_moved*/)
          {
            if (m2_instance->_rendered_last_frame)
            {
              render = true; // skip frustum check
            }
          }
          if (!render && m2_instance->isInRenderDist(_cull_distance, camera_pos, render_settings.display_mode)
            && (tile->renderer()->objectsFrustumCullTest() > 1 || m2_instance->isInFrustum(frustum)))
          {
            render = true;
          }

          if (!render)
            continue;

          instances.emplace_back(m2_instance->transformMatrix());
          m2_instance->_rendered_last_frame = true;


          // if (render && !draw_models_with_box /* && !m2_instance->model->is_hidden()*/)
          // {
          //   // model box wasn't set in model draw(), add selection boxes
          //   if (_world->selected_uids.contains(m2_instance->uid))
          //     model_boxes_to_draw.emplace(m2_instance->model, instances.size());
          // }

        }

      }
      else if (pair.second[0]->which() == eWMO)
      {
        if (!render_settings.draw_wmo)
        {
          for (auto& instance : pair.second)
          {
            instance->_rendered_last_frame = false;
          }
          continue;
        }

        // memory allocation heuristic. all objects will pass if tile is entirely in frustum.
        // otherwise we only allocate for a half

        if (tile->renderer()->objectsFrustumCullTest() > 1)
        {
          wmos_to_draw.reserve(wmos_to_draw.size() + pair.second.size());
        }
        else
        {
          wmos_to_draw.reserve(wmos_to_draw.size() + pair.second.size() / 2);
        }

        for (auto& instance : pair.second)
        {
          instance->_rendered_last_frame = false;

          // do not render twice the cross-referenced objects twice
          if (instance->frame == frame)
          {
            instance->_rendered_last_frame = true;
            continue;
          }

          auto wmo_instance = static_cast<WMOInstance*>(instance);

          if (!render_settings.draw_hidden_models && wmo_instance->wmo->is_hidden())
            continue;

          instance->frame = frame;

          // experimental : if camera and object haven't moved/changed since last frame, we don't need to do frustum culling again
          bool render = false;
          if (!render_settings.camera_moved && !wmo_instance->extentsDirty()/* && not_moved*/)
          {
            if (wmo_instance->_rendered_last_frame)
            {
              render = true; // skip visibility checks
            }
          }
          if ((!render && tile->renderer()->objectsFrustumCullTest() > 1)
              || frustum.intersects(wmo_instance->getExtents()[1], wmo_instance->getExtents()[0]))
          {
            render = true;
          }

          if (render)
          {
            wmos_to_draw.emplace_back(wmo_instance);
            wmo_instance->_rendered_last_frame = true;

            if (render_settings.draw_wmo_doodads)
            {
              // auto doodads = wmo_instance->get_visible_doodads(frustum, _cull_distance, camera_pos, draw_hidden_models, display);
              // 
              // for (auto& doodad : doodads)
              // {
              //     if (doodad->frame == frame)
              //         continue;
              //     doodad->frame = frame;
              // 
              //     auto& instances = models_to_draw[doodad->model.get()];
              // 
              //     instances.emplace_back(doodad->transformMatrix());
              // }

              // doodad->isInFrustum(frustum);

              std::map<uint32_t, std::vector<wmo_doodad_instance>>* doodads = wmo_instance->get_doodads(render_settings.draw_hidden_models);
              
              if (!doodads)
                continue;
              
              for (auto& pair : *doodads)
              {
                for (auto& doodad : pair.second)
                {
                    if (doodad.frame == frame)
                        continue;
                    doodad.frame = frame;

                    // skip no geometry boxes for WMO doodads
                    if (doodad.model->use_fake_geometry())
                      continue;

                    // apply size culling to wmo doodads?
                    float dist = glm::distance(camera_pos, doodad.world_pos) - (doodad.model->bounding_box_radius * doodad.scale);

                    if (!doodad.isInRenderDist(_cull_distance, camera_pos, render_settings.display_mode))
                      continue;
                    // TODO can check if in indoor group & exterior not hidden for further optimization. possibly check portals relations
              
                    auto& instances = models_to_draw[doodad.model.get()];
              
                    instances.emplace_back(doodad.transformMatrix());
                }
              }
            }
          }
        }
      }
    }
  }

  // WMOs / map objects (requires WMO program)
  if ((render_settings.draw_wmo || _world->mapIndex.hasAGlobalWMO()) && _wmo_program)
  {
    ZoneScopedN("World::draw() : Draw WMOs");
    {
      OpenGL::Scoped::use_program wmo_program{*_wmo_program.get()};
      wmo_program.uniform("model_view", model_view);
      wmo_program.uniform("projection", projection);

      wmo_program.uniform("camera", glm::vec3(camera_pos.x, camera_pos.y, camera_pos.z));

      QSettings settings;
      bool const wmo_terrain_blend = settings.value("wmo_terrain_blend", modern_features).toBool();
      wmo_program.uniform("wmo_terrain_blend_enabled", static_cast<int>(wmo_terrain_blend && _terrain_blend_color_tex != 0));
      wmo_program.uniform("terrain_blend_origin_xz", _terrain_blend_origin_xz);
      wmo_program.uniform("terrain_blend_inv_size", _terrain_blend_inv_size);
      wmo_program.uniform("terrain_blend_color", 16);

      gl.activeTexture(GL_TEXTURE0 + 16);
      gl.bindTexture(GL_TEXTURE_2D, wmo_terrain_blend ? _terrain_blend_color_tex : 0);

      // make this check per WMO or global WMO with tiles may not work
      bool disable_cull = false;

      if (_world->mapIndex.hasAGlobalWMO() && !wmos_to_draw.size())
      {
          auto global_wmo = _world->_model_instance_storage.get_wmo_instance(_world->mWmoEntry.uniqueID);
          if (global_wmo.has_value())
          {
            WMOInstance* const gw = global_wmo.value();
            wmos_to_draw.push_back(gw);
            disable_cull = true;
          }
      }


      for (auto& instance: wmos_to_draw)
      {
        bool is_hidden = instance->wmo->is_hidden();

        bool is_exclusion_filtered = false;

        // minimap render exclusion filters
        // per-model
        if (render_settings.minimap_render && minimap_render_settings->use_filters)
        {
          if (instance->instance_model()->file_key().hasFilepath())
          {
            for(int i = 0; i < minimap_render_settings->wmo_model_filter_exclude->count(); ++i)
            {
              auto item = reinterpret_cast<Ui::MinimapWMOModelFilterEntry*>(
                  minimap_render_settings->wmo_model_filter_exclude->itemWidget(
                  minimap_render_settings->wmo_model_filter_exclude->item(i)));

              if (item->getFileName().toStdString() == instance->instance_model()->file_key().filepath())
              {
                is_exclusion_filtered = true;
                break;
              }
            }
          }

          // per-instance
          for(int i = 0; i < minimap_render_settings->wmo_instance_filter_exclude->count(); ++i)
          {
            auto item = reinterpret_cast<Ui::MinimapInstanceFilterEntry*>(
                minimap_render_settings->wmo_instance_filter_exclude->itemWidget(
                minimap_render_settings->wmo_instance_filter_exclude->item(i)));

            if (item->getUid() == instance->uid)
            {
              is_exclusion_filtered = true;
              break;
            }
          }

          // skip model rendering if excluded by filter
          if (is_exclusion_filtered)
            continue;
        }

        bool const is_selected = _world->selected_uids.contains(instance->uid);

        /*if (draw_hidden_models || !is_hidden)*/ // now checking when adding instances
        {
          instance->draw(wmo_program
              , model_view
              , projection
              , frustum
              , _cull_distance
              , camera_pos
              , is_hidden
              , render_settings.draw_wmo_doodads
              , render_settings.draw_fog
              , is_selected
              , _world->animtime
              , _skies->hasSkies()
              , render_settings.display_mode
              , disable_cull
              , render_settings.draw_wmo_exterior
              , render_settings.render_select_wmo_aabb
              , render_settings.render_select_wmo_groups_bounds

          );
        }
      }
    }
  }


  // occlusion culling
  // terrain tiles act as occluders for each other, water and M2/WMOs.
  // occlusion culling is not performed on per model instance basis
  // rendering a little extra is cheaper than querying.
  // occlusion latency has 1-2 frames delay.

  constexpr bool occlusion_cull = true;
  if (occlusion_cull && _occluder_program)
  {
    OpenGL::Scoped::use_program occluder_shader{ *_occluder_program.get() };
    occluder_shader.uniform("model_view", model_view);
    occluder_shader.uniform("projection", projection);

    {
      scoped_color_mask const no_color_write(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
      OpenGL::Scoped::depth_mask_setter<GL_FALSE> const no_depth_write;
      gl.bindVertexArray(_occluder_vao);
      gl.bindBuffer(GL_ELEMENT_ARRAY_BUFFER, _occluder_index);
      gl.disable(GL_CULL_FACE); // TODO: figure out why indices are bad and we need this

      for (auto const& pair : _world->_loaded_tiles_buffer)
      {
        MapTile* tile = pair.second;

        if (!tile)
        {
          break;
        }

        tile->renderer()->setOccluded(!tile->renderer()->getTileOcclusionQueryResult(camera_pos));
        tile->renderer()->doTileOcclusionQuery(occluder_shader);
      }

      gl.enable(GL_CULL_FACE);
      gl.bindVertexArray(0);
      gl.bindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
  }


  // draw occlusion AABBs
  if (render_settings.draw_occlusion_boxes)
  {

    for (auto const& pair : _world->_loaded_tiles_buffer)
    {
      MapTile* tile = pair.second;

      if (!tile)
      {
        break;
      }

      glm::mat4x4 identity_mtx = glm::mat4x4{1};
      auto& extents = tile->getCombinedExtents();
      Noggit::Rendering::Primitives::WireBox::getInstance(_world->_context).draw ( model_view
          , projection
          , identity_mtx
          , { 1.0f, 1.0f, 0.0f, 1.0f }
          , extents[0]
          , extents[1]
      );
    }
  }

  bool draw_doodads_wmo = render_settings.draw_wmo && render_settings.draw_wmo_doodads;
  // M2s / models (requires instanced program)
  if ((render_settings.draw_models || draw_doodads_wmo || (render_settings.minimap_render && minimap_render_settings->use_filters))
      && _m2_instanced_program)
  {
    ZoneScopedN("World::draw() : Draw M2s");

    if (render_settings.draw_model_animations)
    {
      ModelManager::resetAnim();
    }
    /*
    if (_world->need_model_updates)
    {
      _world->update_models_by_filename();
    }*/

    {
      if (render_settings.draw_models || draw_doodads_wmo || (render_settings.minimap_render && minimap_render_settings->use_filters))
      {
        OpenGL::Scoped::use_program m2_shader {*_m2_instanced_program.get()};

        OpenGL::M2RenderState model_render_state;
        model_render_state.tex_arrays = {0, 0};
        model_render_state.tex_indices = {0, 0};
        model_render_state.tex_unit_lookups = {0, 0};
        m2_shader.uniform("model_view", model_view);
        m2_shader.uniform("projection", projection);
        gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl.disable(GL_BLEND);
        gl.depthMask(GL_TRUE);
        gl.enable(GL_CULL_FACE);
        m2_shader.uniform("blend_mode", 0);
        m2_shader.uniform("unfogged", static_cast<int>(model_render_state.unfogged));
        m2_shader.uniform("unlit",  static_cast<int>(model_render_state.unlit));
        m2_shader.uniform("tex_unit_lookup_1", 0);
        m2_shader.uniform("tex_unit_lookup_2", 0);
        m2_shader.uniform("terrain_uv_mask", 0);
        m2_shader.uniform("pixel_shader", 0);

        for (auto const& pair : models_to_draw)
        {
          bool is_inclusion_filtered = false;

          // minimap render inclusion filters
          // per-model
          if (render_settings.minimap_render && minimap_render_settings->use_filters)
          {
            if (pair.first->file_key().hasFilepath())
            {
              for(int i = 0; i < minimap_render_settings->m2_model_filter_include->count(); ++i)
              {
                auto item = reinterpret_cast<Ui::MinimapM2ModelFilterEntry*>(
                    minimap_render_settings->m2_model_filter_include->itemWidget(
                    minimap_render_settings->m2_model_filter_include->item(i)));

                if (item->getFileName().toStdString() == pair.first->file_key().filepath())
                {
                  is_inclusion_filtered = true;
                  break;
                }
              }
            }

            // skip model rendering if excluded by filter
            if (!is_inclusion_filtered)
              continue;
          }

          bool draw_animated_boxes = true;

          /*if (draw_hidden_models || !pair.first->is_hidden())*/ // now done when building models_to_draw
          {
            try
            {
              pair.first->renderer()->draw( model_view
                  , pair.second
                  , m2_shader
                  , model_render_state
                  , frustum
                  , _cull_distance
                  , camera_pos
                  , _world->animtime
                  , render_settings.draw_models_with_box
                  , model_boxes_to_draw
                  , render_settings.display_mode
                  , false
                  , render_settings.draw_model_animations
                  , render_settings.editing_mode == editing_mode::object
                  , draw_animated_boxes
                  , _world
                  , !render_settings.minimap_render
              );
              _world->_n_rendered_objects += pair.second.size();
            }
            catch (std::exception const& e)
            {
              std::string const id = pair.first->file_key().hasFilepath()
                ? pair.first->file_key().filepath()
                : std::to_string(pair.first->file_key().fileDataID());
              LogError << "WorldRender: model draw exception for '" << id << "': " << e.what() << std::endl;
              LogMissingAsset(id, std::string("draw exception: ") + e.what());
              pair.first->error_on_loading();
              continue;
            }
            catch (...)
            {
              std::string const id = pair.first->file_key().hasFilepath()
                ? pair.first->file_key().filepath()
                : std::to_string(pair.first->file_key().fileDataID());
              LogError << "WorldRender: model draw unknown exception for '" << id << "'" << std::endl;
              LogMissingAsset(id, "draw unknown exception");
              pair.first->error_on_loading();
              continue;
            }
          }

          // Draw animated bounding boxes for small animated models that move
          if (/*render_settings.editing_mode == editing_mode::object*/
            (render_settings.draw_models_with_box || pair.first->is_hidden()) // same condition to draw bounding box in draw()
            /*&& render_settings.draw_model_animations*/
            && pair.first->animated_mesh()  && pair.first->mesh_bounds_ratio < 0.5f)
          {
            auto animated_bb = pair.first->getAnimatedBoundingBox();
            for (auto const& instance_matrix : pair.second)
            {
              Noggit::Rendering::Primitives::WireBox::getInstance(_world->_context).draw(model_view
                , projection
                , instance_matrix
                , { 0.6f, 0.6f, 0.6f, 1.0f } // grey
                , animated_bb[0]
                , animated_bb[1]
              );
            }
          }

        }

        /*
        if (draw_doodads_wmo)
        {
          _model_instance_storage.for_each_wmo_instance([&] (WMOInstance& wmo)
            {
              auto doodads = wmo.get_doodads(draw_hidden_models);

              if (!doodads)
                return;

              static std::vector<ModelInstance*> instance_temp = {nullptr};
              for (auto& pair : *doodads)
              {
                for (auto& doodad : pair.second)
                {
                  instance_temp[0] = &doodad;
                  doodad.model->draw( model_view
                    , instance_temp
                    , m2_shader
                    , model_render_state
                    , frustum
                    , culldistance
                    , camera_pos
                    , animtime
                    , draw_models_with_box
                    , model_boxes_to_draw
                    , display
                  );
                }

              }
            });
        }

                  */
      }

    }

    gl.disable(GL_BLEND);
    gl.enable(GL_CULL_FACE);
    gl.depthMask(GL_TRUE);


    // unsigned int wmos_todraw_count = wmos_to_draw.size();
    // unsigned int models_todraw_count = models_to_draw.size();
    _world->_n_rendered_objects += wmos_to_draw.size();

    models_to_draw.clear();
    wmos_to_draw.clear();

    // draw model boxes with m2 box shader
    // if(draw_models_with_box || (draw_hidden_models && !model_boxes_to_draw.empty()))
    if (!render_settings.minimap_render && !model_boxes_to_draw.empty() && _m2_box_program)
    {
      OpenGL::Scoped::use_program m2_box_shader{ *_m2_box_program.get() };

      OpenGL::Scoped::bool_setter<GL_LINE_SMOOTH, GL_TRUE> const line_smooth;
      gl.hint (GL_LINE_SMOOTH_HINT, GL_NICEST);

      for (auto const& it : model_boxes_to_draw)
      {
        glm::vec4 color = it.first->is_hidden()
                          ? glm::vec4(0.f, 0.f, 1.f, 1.f)
                          : ( it.first->use_fake_geometry()
                              ? glm::vec4(1.f, 0.f, 0.f, 1.f)
                              : glm::vec4(0.75f, 0.75f, 0.75f, 1.f)
                          )
        ;

        m2_box_shader.uniform("color", color);
        it.first->renderer()->drawBox(m2_box_shader, it.second);
      }
    }
    model_boxes_to_draw.clear();

    // render m2 selection boxes.
    // TODO can try to move to m2 box shader but it requires some refactor
    if (!render_settings.minimap_render)
    {
      for (auto const& selection : _world->current_selection())
      {
        if (selection.index() == eEntry_Object)
        {
          auto const obj = std::get<selected_object_type>(selection);
      
          if (obj->which() != eMODEL)
            continue;
      
          ModelInstance* model = static_cast<ModelInstance*>(obj);

          // if (model->_rendered_last_frame)
          {
            // bool is_selected = false;
            bool is_selected = _world->is_selected(model->uid);

            bool draw_anim_bb = !(render_settings.draw_models_with_box || model->model->is_hidden());
      
            model->draw_box(model_view, projection, is_selected, render_settings.render_select_m2_collission_bbox
              , render_settings.render_select_m2_aabb, draw_anim_bb);
          }
        }
      }
    }
  }

  // render selection group boxes
  if (!render_settings.minimap_render)
  {
    for (auto const& selection_group : _world->_selection_groups)
    {
        if (!selection_group.isSelected())
            continue;

        glm::mat4x4 identity_mtx = glm::mat4x4{ 1 };
        auto const& extents = selection_group.getExtents();
        Noggit::Rendering::Primitives::WireBox::getInstance(_world->_context).draw(model_view
            , projection
            , identity_mtx
            , { 0.0f, 0.0f, 1.0f, 1.0f } // blue
            , extents[0]
            , extents[1]
        );
    }
  }

  /*
  // model particles
  if (draw_model_animations && !model_with_particles.empty())
  {
    OpenGL::Scoped::bool_setter<GL_CULL_FACE, GL_FALSE> const cull;
    OpenGL::Scoped::depth_mask_setter<GL_FALSE> const depth_mask;

    OpenGL::Scoped::use_program particles_shader {*_m2_particles_program.get()};

    particles_shader.uniform("model_view_projection", mvp);
    OpenGL::texture::set_active_texture(0);

    for (auto& it : model_with_particles)
    {
      it.first->draw_particles(model_view, particles_shader, it.second);
    }
  }


  if (draw_model_animations && !model_with_particles.empty())
  {
    OpenGL::Scoped::bool_setter<GL_CULL_FACE, GL_FALSE> const cull;
    OpenGL::Scoped::depth_mask_setter<GL_FALSE> const depth_mask;

    OpenGL::Scoped::use_program ribbon_shader {*_m2_ribbons_program.get()};

    ribbon_shader.uniform("model_view_projection", mvp);

    gl.blendFunc(GL_SRC_ALPHA, GL_ONE);

    for (auto& it : model_with_particles)
    {
      it.first->draw_ribbons(ribbon_shader, it.second);
    }
  }

   */

  gl.enable(GL_BLEND);
  gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // render before the water and enable depth right 
  // so it's visible under water
  // the checker board pattern is used to see the water under it
  if (render_settings.angled_mode || render_settings.use_ref_pos)
  {
    ZoneScopedN("World::draw() : Draw angles");
    // OpenGL::Scoped::bool_setter<GL_CULL_FACE, GL_FALSE> cull;
    OpenGL::Scoped::depth_mask_setter<GL_TRUE> const depth_mask;

    math::degrees orient = math::degrees(render_settings.orientation);
    math::degrees incl = math::degrees(render_settings.angle);
    glm::vec4 color = cursor_color;
    // color.w = 0.5f;
    color.w = 0.75f;

    float radius = 1.2f * render_settings.brush_radius;

    if (render_settings.angled_mode && render_settings.editing_mode == editing_mode::flatten_blur)
    {
      if (render_settings.angle > 49.0f) // 0.855 radian
      {
        color.x = 1.f;
        color.y = 0.f;
        color.z = 0.f;
      }
    }

    if (render_settings.angled_mode && !render_settings.use_ref_pos)
    {
      glm::vec3 pos = cursor_pos;
      pos.y += 0.1f; // to avoid z-fighting with the ground
      _square_render.draw(mvp, pos, radius, incl, orient, color);
    }
    else if (render_settings.use_ref_pos)
    {
      if (render_settings.angled_mode)
      {
        glm::vec3 pos = cursor_pos;
        pos.y = misc::angledHeight(ref_pos, pos, incl, orient);
        pos.y += 0.1f;
        _square_render.draw(mvp, pos, radius, incl, orient, color);

        // display the plane when the cursor is far from ref_point
        if (misc::dist(pos.x, pos.z, ref_pos.x, ref_pos.z) > 10.f + radius)
        {
          glm::vec3 ref = ref_pos;
          ref.y += 0.1f;
          _square_render.draw(mvp, ref, 10.f, incl, orient, color);
        }
      }
      else
      {
        glm::vec3 pos = cursor_pos;
        pos.y = ref_pos.y + 0.1f;
        _square_render.draw(mvp, pos, radius, math::degrees(0.f), math::degrees(0.f), color);
      }
    }
  }

  if (render_settings.draw_water && _liquid_program)
  {
    ZoneScopedN("World::draw() : Draw water");

    // draw the water on both sides
    OpenGL::Scoped::bool_setter<GL_CULL_FACE, GL_FALSE> const cull;

    OpenGL::Scoped::use_program water_shader{ *_liquid_program.get()};

    gl.bindVertexArray(_liquid_chunk_vao);

    water_shader.uniform("model_view", model_view);
    water_shader.uniform("projection", projection);
    water_shader.uniform("camera", glm::vec3(camera_pos.x, camera_pos.y, camera_pos.z));
    water_shader.uniform("animtime", _world->animtime);
    // liquid_vert.glsl: when use_transform is 1 it multiplies by `transform`; we never set that for world ADT water,
    // so leaving use_transform on breaks rendering whenever WMOs are drawn / global WMO maps.
    water_shader.uniform("transform", glm::mat4x4(1.f));
    water_shader.uniform("use_transform", 0);

    for (auto& pair : _world->_loaded_tiles_buffer)
    {
      MapTile* tile = pair.second;

      if (!tile)
        break;

      if (tile->renderer()->isOccluded() && !tile->Water.needsUpdate() && !tile->renderer()->isOverridingOcclusionCulling())
        continue;

      tile->Water.renderer()->draw(
          frustum
          , camera_pos
          , render_settings.camera_moved
          , water_shader
          , _world->animtime
          , render_settings.water_layer
          , render_settings.display_mode
          , &_liquid_texture_manager
      );
    }

    gl.bindVertexArray(0);
  }

  gl.enable(GL_BLEND);

  // draw last because of the transparency
  if (render_settings.draw_mfbo && _mfbo_program)
  {
    ZoneScopedN("World::draw() : Draw flight bounds");
    // don't write on the depth buffer
    OpenGL::Scoped::depth_mask_setter<GL_FALSE> const depth_mask;

    OpenGL::Scoped::use_program mfbo_shader {*_mfbo_program.get()};

    for (MapTile* tile : _world->mapIndex.loaded_tiles())
    {
      if (tile->hasFlightBounds())
      {
        tile->flightBoundsRenderer()->draw(mfbo_shader);
      }
    }
  }

  if (render_settings.editing_mode == editing_mode::light && render_settings.alpha_light_sphere > 0.0f)
  {
    // Sky* CurrentSky = skies()->findClosestSkyByDistance(camera_pos);
    // Sky* CurrentSky = skies()->findClosestSkyByWeight();
    // if (!CurrentSky)
    //     return;

    // bad design, there can be multiple current skies, this is only the highest one.
    // all skies we're inside of need to be drawn with front culling
    // int CurrentSkyID = CurrentSky->Id;
        
    const int MAX_TIME_VALUE_C = 2880;
    const int CurrenTime = static_cast<int>(_world->time) % MAX_TIME_VALUE_C;

    // draw Light Zones
    for (auto const& zoneLight : skies()->zoneLightsWotlk)
    {
      Sky* light = skies()->findSkyById(zoneLight.lightId);

      assert(light != nullptr);

      if (glm::distance(light->pos, camera_pos) > (_cull_distance + light->r2) ) // TODO: frustum cull here
        continue;

      glm::vec4 diffuse = { light->colorFor(LIGHT_GLOBAL_DIFFUSE, CurrenTime), 1.f };
      // glm::vec4 ambient = { light->colorFor(LIGHT_GLOBAL_AMBIENT, CurrenTime), 1.f };

      // Render Points
      auto const& zoneLightPoints = zoneLight.points; // skies()->zoneLightPoints[zoneLight.second.id];

      // polygon must have at least 3 points
      if (zoneLightPoints.size() < 3)
        continue;

      std::vector<glm::vec3> lineRenderPoints;

      for (int point_id = 0; point_id < zoneLightPoints.size(); point_id++)
      {
        glm::vec2 const curr_point = zoneLightPoints[point_id];

        // using light z/y pos to set the sphere position, those are supposed to be planes from point to point with infinite height.
        glm::vec3 point_pos = glm::vec3(curr_point.x, light->pos.y, curr_point.y);
        lineRenderPoints.push_back(point_pos);

        // can render a sphere at each point
        // float sphere_radius = 10.f;
        // _sphere_render.draw(mvp, point_pos, diffuse, sphere_radius, 32, 18, alpha_light_sphere, false, false);

        // Connect last point to the first
        if (point_id == (zoneLightPoints.size() - 1))
        {
          lineRenderPoints.push_back(lineRenderPoints[0]);
        }
      }
      _line_render.draw(mvp, lineRenderPoints, diffuse, false); // glm::vec4(1.f, 0.f, 0.f, 1.f) red

      // debug testing, only render first zone
      // break;

      // TODO render a vertical rectangle between each points to draw the polygon in 3D
    }

    // Draw Sky/Light spheres
    glCullFace(GL_FRONT);
    if (!render_settings.draw_only_inside_light_sphere)
    {
      for (Sky const& sky : skies()->skies)
      {
        // we draw skies we're inside of later with glCullFace(GL_BACK);
        if (/*CurrentSkyID == sky.Id || */sky.weight > 0.0f || sky.global)
          continue;

        if (glm::distance(sky.pos, camera_pos) <= _cull_distance) // TODO: frustum cull here
        {
          glm::vec4 diffuse = { sky.colorFor(LIGHT_GLOBAL_DIFFUSE, CurrenTime), 1.0f };
          glm::vec4 ambient = { sky.colorFor(LIGHT_GLOBAL_AMBIENT, CurrenTime), 1.0f };

          _sphere_render.draw(mvp, sky.pos, ambient, sky.r1, 32, 18
            , render_settings.alpha_light_sphere, false, render_settings.draw_wireframe_light_sphere);
          _sphere_render.draw(mvp, sky.pos, diffuse, sky.r2, 32, 18
            , render_settings.alpha_light_sphere, false, render_settings.draw_wireframe_light_sphere);
        
          // special wirebox to highlight zone lights
          if (sky.zone_light)
          {
            glm::vec3 minExtent =  glm::vec3(sky.pos.x - sky.r2, sky.pos.y - sky.r2, sky.pos.z - sky.r2);
            glm::vec3 maxExtent = glm::vec3(sky.pos.x + sky.r2, sky.pos.y + sky.r2, sky.pos.z + sky.r2);

            _wirebox_render.draw(model_view, projection, glm::mat4x4{ 1 }, { 1.0f, 1.0f, 1.0f, 1.0f },
                        minExtent, maxExtent);
          }

          // TODO Those lines tank fps by 50%
          // std::vector<glm::vec3> linePoints;
          // linePoints.push_back(glm::vec3(sky.pos.x, sky.pos.y, sky.pos.z - sky.r2));
          // linePoints.push_back(glm::vec3(sky.pos.x, sky.pos.y, sky.pos.z + sky.r2));
          // _line_render.draw(mvp, linePoints, glm::vec4(1.f), false);
        }
      }
    }

    // now draw the current light (light that we're inside of)
    glCullFace(GL_BACK);
    for (Sky const& sky : skies()->skies)
    {
      if (sky.global)
        continue;
      if (/*CurrentSky->getId() == sky.Id ||*/ sky.weight > 0.0f)
      {
        glm::vec4 diffuse = { sky.colorFor(LIGHT_GLOBAL_DIFFUSE, CurrenTime), 1.0f };
        glm::vec4 ambient = { sky.colorFor(LIGHT_GLOBAL_AMBIENT, CurrenTime), 1.0f };

        // always render wireframe in the current light
        // need to render outer first or it gets culled
        _sphere_render.draw(mvp, sky.pos, diffuse, sky.r2, 32, 18
          , render_settings.alpha_light_sphere, true, false);
        _sphere_render.draw(mvp, sky.pos, ambient, sky.r1, 32, 18
          , render_settings.alpha_light_sphere, true, false);


        // std::vector<glm::vec3> linePoints;
        // linePoints.push_back(glm::vec3(CurrentSky->pos.x, CurrentSky->pos.z, CurrentSky->pos.y - CurrentSky->r2));
        // linePoints.push_back(glm::vec3(CurrentSky->pos.x, CurrentSky->pos.z, CurrentSky->pos.y + CurrentSky->r2));
        // _line_render.draw(mvp, linePoints, glm::vec4(1.f, 0.f, 0.f, 1.f), false);
      }
    }
  }

  // Draw point light visualization spheres after scene geometry so opaque draws do not
  // stomp them. Use a depth prepass (color mask off, depth write on) then blended color
  // (depth write off) so transparent fragments still participate in depth testing against
  // the scene correctly (avoids "seeing through" opaque geometry).
  if (render_settings.draw_point_light_spheres)
  {
    ZoneScopedN("World::draw() : Draw point light spheres");
    float constexpr k_point_light_sphere_radius = 0.50f;

    OpenGL::Scoped::bool_setter<GL_DEPTH_TEST, GL_TRUE> const enable_depth_test;
    gl.depthFunc (GL_LEQUAL);

    std::optional<std::size_t> const sel = _world->selectedPointLightIndex();

    for (std::size_t li = 0; li < _world->_point_lights.size(); ++li)
    {
      auto const& light = _world->_point_lights[li];
      float const sphere_alpha = (sel && *sel == li) ? 1.f : 0.5f;
      float constexpr cone_alpha = 0.5f;

      glm::vec4 const color = { light.color, 1.f };

      {
        OpenGL::Scoped::bool_setter<GL_BLEND, GL_FALSE> const disable_blend_prepass;
        scoped_color_mask const no_color_write (GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        OpenGL::Scoped::depth_mask_setter<GL_TRUE> const write_depth_prepass;

        _sphere_render.draw(
            mvp,
            light.position,
            color,
            k_point_light_sphere_radius,
            32,
            18,
            1.f,
            render_settings.draw_wireframe_light_sphere
        );
      }

      {
        OpenGL::Scoped::depth_mask_setter<GL_FALSE> const no_depth_write;
        OpenGL::Scoped::bool_setter<GL_BLEND, GL_TRUE> const enable_blend;
        gl.blendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        _sphere_render.draw(
            mvp,
            light.position,
            color,
            k_point_light_sphere_radius,
            32,
            18,
            sphere_alpha,
            render_settings.draw_wireframe_light_sphere
        );
      }

      std::vector<glm::vec3> cone_lines;
      if (light.light_type == World::MapLightType::Spot)
      {
        glm::vec3 const fwd = map_light_forward (light);
        append_light_cone_lines (cone_lines
                                , light.position
                                , fwd
                                , spot_effective_cone_length (light)
                                , light.outer_angle
                                , 24);
      }

      if (!cone_lines.empty())
      {
        OpenGL::Scoped::depth_mask_setter<GL_FALSE> const no_depth_write;
        OpenGL::Scoped::bool_setter<GL_BLEND, GL_TRUE> const enable_blend;
        gl.blendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glm::vec4 const line_color { light.color, cone_alpha };
        // One batched GL_LINES draw (append_light_cone_lines stores independent segment pairs).
        // GL_LINES = 0x0001 — avoids dozens of Line::draw calls per light (each used to recompile shaders).
        _line_render.draw (mvp, cone_lines, line_color, false, 0x0001);
      }
    }
  }
}

void WorldRender::setupMccvVizBuffers()
{
  ZoneScopedN("WorldRender::setupMccvVizBuffers");

  _mccv_viz_program.reset(
    new OpenGL::program{
        { GL_VERTEX_SHADER, OpenGL::shader::src_from_qrc("mccv_viz_vs") },
        { GL_FRAGMENT_SHADER, OpenGL::shader::src_from_qrc("mccv_viz_fs") },
    });

  _mccv_crosshair_ndc_program.reset(
    new OpenGL::program{
        { GL_VERTEX_SHADER, OpenGL::shader::src_from_qrc("mccv_crosshair_vs") },
        { GL_FRAGMENT_SHADER, OpenGL::shader::src_from_qrc("mccv_crosshair_fs") },
    });

  static constexpr glm::vec2 quad_verts[6] = {
      { -1.f, -1.f }, { 1.f, -1.f }, { -1.f, 1.f },
      { -1.f, 1.f },  { 1.f, -1.f }, { 1.f, 1.f },
  };

  gl.bufferData<GL_ARRAY_BUFFER>(_mccv_viz_quad_vbo, sizeof(quad_verts), quad_verts, GL_STATIC_DRAW);

  glm::vec2 cross_init[2]{ { 0.f, 0.f }, { 0.f, 0.f } };
  gl.bufferData<GL_ARRAY_BUFFER>(_mccv_viz_crosshair_vbo, sizeof(cross_init), cross_init, GL_DYNAMIC_DRAW);

  {
    OpenGL::Scoped::vao_binder const vao_bind(_mccv_viz_vao);
    OpenGL::Scoped::use_program viz{ *_mccv_viz_program.get() };

    viz.attrib("quad_corner", _mccv_viz_quad_vbo, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    viz.attrib_divisor("quad_corner", 0, 1);

    viz.attrib("instance_pos", _mccv_viz_instance_vbo, 4, GL_FLOAT, GL_FALSE, sizeof(MccvVizInstance), nullptr);
    viz.attrib_divisor("instance_pos", 1, 1);

    viz.attrib("instance_color", _mccv_viz_instance_vbo, 4, GL_FLOAT, GL_FALSE, sizeof(MccvVizInstance),
               reinterpret_cast<void*>(sizeof(glm::vec4)));
    viz.attrib_divisor("instance_color", 1, 1);
  }

  {
    OpenGL::Scoped::vao_binder const vao_bind(_mccv_crosshair_vao);
    OpenGL::Scoped::use_program ch{ *_mccv_crosshair_ndc_program.get() };
    ch.attrib("ndc_pos", _mccv_viz_crosshair_vbo, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  }

  _mccv_viz_buffers_ready = true;
}

void WorldRender::drawMccvVertexAltViz ( glm::mat4x4 const& model_view
                                       , glm::mat4x4 const& projection
                                       , glm::mat4x4 const& mvp
                                       , glm::vec3 const& camera_pos
                                       , glm::vec4 const& cursor_color
                                       , WorldRenderParams const& render_settings
                                       )
{
  if (!render_settings.draw_mccv_vertex_alt_viz || render_settings.minimap_render)
  {
    return;
  }
  if (!_mccv_viz_buffers_ready || !_mccv_viz_program || !_mccv_crosshair_ndc_program)
  {
    return;
  }
  if (!render_settings.mccv_viz_hub_valid)
  {
    return;
  }

  ZoneScopedN("World::drawMccvVertexAltViz");

  glm::vec3 const hub = render_settings.mccv_viz_hub;
  float const r = std::max(0.01f, render_settings.mccv_viz_radius);
  float const r2 = r * r;

  float const point_radius =
    kMccvVizBallScale
    * glm::clamp(0.22f * glm::distance(camera_pos, hub) / 120.f, 0.12f, 1.6f);
  // Camera-facing quads extend ~point_radius laterally; a tiny normal bump leaves half the disk
  // below the surface. Lift along normal + toward the eye scales with disk size.
  float const n_lift = glm::max(0.18f, 0.42f * point_radius);
  float const v_lift = 0.52f * point_radius;

  std::vector<MccvVizInstance> instances;
  instances.reserve(4096);

  for (MapTile* tile : _world->mapIndex.loaded_tiles())
  {
    if (!tile || !tile->finishedLoading())
    {
      continue;
    }

    std::vector<MapChunk*> const chunks = tile->chunks_in_range(hub, r);
    for (MapChunk* chunk : chunks)
    {
      if (!chunk)
      {
        continue;
      }

      glm::vec3 const default_c(1.f, 1.f, 1.f);

        for (int i = 0; i < mapbufsize; ++i)
      {
        glm::vec3 const& v = chunk->mVertices[i];
        glm::vec2 const d(hub.x - v.x, hub.z - v.z);
        if (glm::dot(d, d) > r2)
        {
          continue;
        }

        glm::vec3 const c = chunk->hasColors() ? chunk->mccv[i] : default_c;

          // MCCV alt-viz needs a stable vertex normal. `MapChunk::mNormals` isn't
          // consistently initialized, so validate it and fall back to a computed normal.
          glm::vec3 n = chunk->mNormals[i];
          float const nlen2 = glm::dot(n, n);
          if (!std::isfinite(nlen2) || nlen2 < 1e-6f || nlen2 > 4.f)
          {
            glm::vec3 const P1 = chunk->getNeighborVertex(i, 0); // up_left
            glm::vec3 const P2 = chunk->getNeighborVertex(i, 1); // up_right
            glm::vec3 const P3 = chunk->getNeighborVertex(i, 2); // down_left
            glm::vec3 const P4 = chunk->getNeighborVertex(i, 3); // down_right

            glm::vec3 const N1 = glm::cross((P2 - v), (P1 - v));
            glm::vec3 const N2 = glm::cross((P3 - v), (P2 - v));
            glm::vec3 const N3 = glm::cross((P4 - v), (P3 - v));
            glm::vec3 const N4 = glm::cross((P1 - v), (P4 - v));

            glm::vec3 Norm = N1 + N2 + N3 + N4;
            float const normlen2 = glm::dot(Norm, Norm);
            if (!std::isfinite(normlen2) || normlen2 < 1e-10f)
            {
              Norm = glm::vec3(0.f, 1.f, 0.f);
            }
            else
            {
              Norm /= std::sqrt(normlen2);
            }
            n = Norm;
          }
          else
          {
            // mNormals should already be unit-ish, but normalize anyway for stable lift scaling.
            n /= std::sqrt(nlen2);
          }

        glm::vec3 V = camera_pos - v;
        float const vlen = glm::length(V);
        if (vlen > 1e-4f)
        {
          V /= vlen;
        }
        else
        {
          V = n;
        }
        glm::vec3 const p = v + n * n_lift + V * v_lift;

          if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
          {
            continue;
          }

          instances.push_back({ glm::vec4(p, 1.f), glm::vec4(c, 0.95f) });
        if (instances.size() >= kMaxMccvVizInstances)
        {
          goto mccv_gather_done;
        }
      }
    }
  }
mccv_gather_done:

  gl.enable(GL_BLEND);
  gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  OpenGL::Scoped::bool_setter<GL_CULL_FACE, GL_FALSE> const cull_off;
  OpenGL::Scoped::depth_mask_setter<GL_FALSE> const no_depth_write;

  if (!instances.empty())
  {
    gl.bufferData<GL_ARRAY_BUFFER>(_mccv_viz_instance_vbo,
                                    static_cast<GLsizeiptr>(instances.size() * sizeof(MccvVizInstance)),
                                    instances.data(),
                                    GL_DYNAMIC_DRAW);

    OpenGL::Scoped::bool_setter<GL_DEPTH_TEST, GL_FALSE> const mccv_viz_no_depth;

    OpenGL::Scoped::use_program viz{ *_mccv_viz_program.get() };
    viz.uniform("model_view", model_view);
    viz.uniform("projection", projection);
    viz.uniform("camera_pos", camera_pos);
    viz.uniform("point_radius", point_radius);

    OpenGL::Scoped::vao_binder const vao_bind(_mccv_viz_vao);
    gl.drawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(instances.size()));
  }

  {
    glm::vec3 const to_cam = camera_pos - hub;
    float const d = glm::length(to_cam);
    glm::vec3 hub_mark = hub;
    if (d > 1e-4f)
    {
      hub_mark += (to_cam / d) * glm::max(0.1f, 0.45f * point_radius);
    }
    else
    {
      hub_mark.y += glm::max(0.12f, 0.4f * point_radius);
    }
    float const s =
      kMccvVizBallScale
      * glm::clamp(0.24f * glm::distance(camera_pos, hub) / 100.f, 0.12f, 1.4f);
    _sphere_render.draw(mvp, hub_mark, cursor_color, s);
  }

  {
    float const leg = 0.035f;
    glm::vec2 const c = render_settings.mccv_viz_hub_ndc;
    std::array<glm::vec2, 4> const verts = { {
        { c.x - leg, c.y },
        { c.x + leg, c.y },
        { c.x, c.y - leg },
        { c.x, c.y + leg },
    } };

    gl.bufferData<GL_ARRAY_BUFFER>(_mccv_viz_crosshair_vbo, verts.size() * sizeof(glm::vec2), verts.data(), GL_DYNAMIC_DRAW);

    OpenGL::Scoped::bool_setter<GL_DEPTH_TEST, GL_FALSE> const no_depth;
    OpenGL::Scoped::use_program ch{ *_mccv_crosshair_ndc_program.get() };
    ch.uniform("color", glm::vec4(1.f, 1.f, 1.f, 1.f));

    OpenGL::Scoped::vao_binder const vao_bind(_mccv_crosshair_vao);
    gl.lineWidth(2.f);
    if (QOpenGLContext* ctx = QOpenGLContext::currentContext())
    {
      ctx->functions()->glDrawArrays(GL_LINES, 0, 4);
    }
  }

  gl.disable(GL_BLEND);
}

void WorldRender::drawRampPreview(glm::mat4x4 const& mvp, WorldRenderParams const& render_settings)
{
  if (!render_settings.draw_ramp_preview || render_settings.minimap_render)
  {
    return;
  }
  if (render_settings.display_mode != display_mode::in_3D)
  {
    return;
  }

  glm::vec3 const& A = render_settings.ramp_preview_a;
  glm::vec3 const& B = render_settings.ramp_preview_b;
  glm::vec2 const ab(B.x - A.x, B.z - A.z);
  float const L = glm::length(ab);
  if (L < 1e-3f || render_settings.ramp_preview_radius <= 0.f)
  {
    return;
  }

  glm::vec2 const f2 = ab / L;
  glm::vec3 const forward(f2.x, 0.f, f2.y);
  glm::vec3 const r(-forward.z * render_settings.ramp_preview_radius, 0.f, forward.x * render_settings.ramp_preview_radius);

  auto point_at = [&](float tAlong) -> glm::vec3
  {
    glm::vec3 p = A + forward * tAlong;
    p.y = glm::mix(A.y, B.y, tAlong / L);
    return p;
  };

  float const cap = std::min(render_settings.ramp_preview_cap_len, L * 0.45f);
  float const t1 = cap;
  float const t2 = L - cap;

  glm::vec3 const cA0 = point_at(0.f) + r;
  glm::vec3 const cB0 = point_at(L) + r;
  glm::vec3 const cB1 = point_at(L) - r;
  glm::vec3 const cA1 = point_at(0.f) - r;

  glm::vec3 const C1 = point_at(t1);
  glm::vec3 const i1a = C1 + r;
  glm::vec3 const i1b = C1 - r;
  glm::vec3 const C2 = point_at(t2);
  glm::vec3 const i2a = C2 + r;
  glm::vec3 const i2b = C2 - r;

  glm::vec4 const col(1.f, 1.f, 1.f, 0.95f);
  OpenGL::Scoped::bool_setter<GL_LINE_SMOOTH, GL_TRUE> const line_smooth;
  gl.hint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  gl.lineWidth(2.f);

  std::vector<glm::vec3> const outer{ cA0, cB0, cB1, cA1, cA0 };
  _line_render.draw(mvp, outer, col, false);
  std::vector<glm::vec3> const cut1{ i1a, i1b };
  _line_render.draw(mvp, cut1, col, false);
  std::vector<glm::vec3> const cut2{ i2a, i2b };
  _line_render.draw(mvp, cut2, col, false);
}

void WorldRender::drawChunkManipulatorSelection ( glm::mat4x4 const& model_view
                                                , glm::mat4x4 const& projection
                                                , WorldRenderParams const& render_settings
                                                )
{
  // Selection and preview are drawn as a terrain tint via the terrain shader (see TileRender overlay masks).
  (void)model_view;
  (void)projection;
  (void)render_settings;
}

void WorldRender::unload()
{
  ZoneScoped;
  _mcnk_program.reset();
  _mfbo_program.reset();
  _m2_program.reset();
  _m2_instanced_program.reset();
  _m2_particles_program.reset();
  _m2_ribbons_program.reset();
  _m2_box_program.reset();
  _wmo_program.reset();
  _liquid_program.reset();

  _mccv_viz_program.reset();
  _mccv_crosshair_ndc_program.reset();
  _mccv_viz_buffers_ready = false;

  _cursor_render.unload();
  _sphere_render.unload();
  _square_render.unload();
  _line_render.unload();
  _wirebox_render.unload();

  _horizon_render.reset();

  _liquid_texture_manager.unload();

  _skies->unload();

  _buffers.unload();
  _vertex_arrays.unload();

  Noggit::Rendering::Primitives::WireBox::getInstance(_world->_context).unload();
}


void WorldRender::updateMVPUniformBlock(const glm::mat4x4& model_view, const glm::mat4x4& projection)
{
  ZoneScoped;

  _mvp_ubo_data.model_view = model_view;
  _mvp_ubo_data.projection = projection;
}

void WorldRender::updateLightingUniformBlock(bool draw_fog, glm::vec3 const& camera_pos)
{
  ZoneScoped;

  _outdoor_light_stats = _outdoor_lighting->getLightStats(static_cast<int>(_world->time));

  glm::vec3 diffuse = _skies->color_set[LIGHT_GLOBAL_DIFFUSE];
  glm::vec3 ambient = _skies->color_set[LIGHT_GLOBAL_AMBIENT];
  glm::vec3 fog_color = _skies->color_set[SKY_FOG_COLOR];
  glm::vec3 ocean_color_light = _skies->color_set[OCEAN_COLOR_LIGHT];
  glm::vec3 ocean_color_dark = _skies->color_set[OCEAN_COLOR_DARK];
  glm::vec3 river_color_light = _skies->color_set[RIVER_COLOR_LIGHT];
  glm::vec3 river_color_dark = _skies->color_set[RIVER_COLOR_DARK];


  _lighting_ubo_data.DiffuseColor_FogStart = {diffuse.x,diffuse.y,diffuse.z, _skies->fog_distance_start()};
  _lighting_ubo_data.AmbientColor_FogEnd = {ambient.x,ambient.y,ambient.z, _skies->fog_distance_end()};
  _lighting_ubo_data.FogColor_FogOn = {fog_color.x,fog_color.y,fog_color.z, static_cast<float>(draw_fog)};

  if (directional_lightning)
    _lighting_ubo_data.LightDir_FogRate = { _outdoor_light_stats.dayDir.x, _outdoor_light_stats.dayDir.y, _outdoor_light_stats.dayDir.z, _skies->fogRate() };
  else
    _lighting_ubo_data.LightDir_FogRate = {0.0f, -1.0f, 0.0f, _skies->fogRate()};

  _lighting_ubo_data.OceanColorLight = { ocean_color_light.x,ocean_color_light.y,ocean_color_light.z, _skies->ocean_shallow_alpha()};
  _lighting_ubo_data.OceanColorDark = { ocean_color_dark.x,ocean_color_dark.y,ocean_color_dark.z, _skies->ocean_deep_alpha()};
  _lighting_ubo_data.RiverColorLight = { river_color_light.x,river_color_light.y,river_color_light.z, _skies->river_shallow_alpha()};
  _lighting_ubo_data.RiverColorDark = { river_color_dark.x,river_color_dark.y,river_color_dark.z, _skies->river_deep_alpha()};

  gl.bindBuffer(GL_UNIFORM_BUFFER, _lighting_ubo);
  gl.bufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(OpenGL::LightingUniformBlock), &_lighting_ubo_data);
}

void WorldRender::updatePointLightsUniformBlock(bool enabled, glm::vec3 const& camera_pos)
{
  ZoneScoped;

  _point_lights_ubo_data.meta = { 0, enabled ? 1 : 0, 0, 0 };

  if (enabled)
  {
    // Hard cap at kMaxGpuPointLights; pick the closest lights to the camera so the set is stable
    // and relevant. Do not cull by (camera ↔ light) distance: a light far from the camera can
    // still illuminate terrain and models in front of the viewer within attenuation_end.
    int count = 0;
    float const flicker_t = _world->animtime * 0.001f;

    std::vector<std::size_t> order;
    {
      std::size_t const n = _world->_point_lights.size();
      order.resize (n);
      for (std::size_t i = 0; i < n; ++i)
        order[i] = i;
      std::sort (order.begin(), order.end(),
                 [&] (std::size_t a, std::size_t b)
                 {
                   return glm::distance (_world->_point_lights[a].position, camera_pos)
                        < glm::distance (_world->_point_lights[b].position, camera_pos);
                 });
    }

    for (std::size_t const li : order)
    {
      if (count >= OpenGL::kMaxGpuPointLights)
        break;

      auto const& light = _world->_point_lights[li];

      float radius = std::max(0.0f, light.attenuation_end);
      if (light.light_type == World::MapLightType::Spot)
        radius = std::max (radius, spot_effective_cone_length (light));

      float const flicker_mul = point_light_intensity_multiplier (light, flicker_t);
      float const eff_intensity = light.intensity * flicker_mul;

      _point_lights_ubo_data.position_radius[count] = { light.position, radius };
      _point_lights_ubo_data.color_intensity[count] = { light.color, eff_intensity };
      _point_lights_ubo_data.attenuation[count] = { light.attenuation_start, light.attenuation_end, 0.f, 0.f };

      if (light.light_type == World::MapLightType::Spot)
      {
        glm::vec3 const fwd = map_light_forward (light);
        float const ci = std::cos (light.inner_angle);
        float const co = std::cos (light.outer_angle);
        _point_lights_ubo_data.spot_dir_cos_inner[count] = { fwd, ci };
        _point_lights_ubo_data.spot_cos_outer_kind[count] = { co, 1.f, 0.f, 0.f };
      }
      else
      {
        _point_lights_ubo_data.spot_dir_cos_inner[count] = { 0.f, 0.f, 1.f, 1.f };
        _point_lights_ubo_data.spot_cos_outer_kind[count] = { -1.f, 0.f, 0.f, 0.f };
      }

      count++;
    }

    _point_lights_ubo_data.meta.x = count;
  }

  gl.bindBuffer(GL_UNIFORM_BUFFER, _point_lights_ubo);
  gl.bufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(OpenGL::PointLightsUniformBlock), &_point_lights_ubo_data);
}

void WorldRender::updateLightingUniformBlockMinimap(MinimapRenderSettings* settings)
{
  ZoneScoped;

  glm::vec3 diffuse = settings->diffuse_color;
  glm::vec3 ambient = settings->ambient_color;

  _lighting_ubo_data.FogColor_FogOn = { 0, 0, 0, 0 };
  if (settings->export_mode == MinimapGenMode::LOD_MAPTEXTURES) {
      _lighting_ubo_data.DiffuseColor_FogStart = { 0.5, 0.5, 0.5, 0 };
      _lighting_ubo_data.AmbientColor_FogEnd = { 0.5, 0.5, 0.5, 0 };
      _lighting_ubo_data.LightDir_FogRate = { 0.0, -1.0, 0.0, _skies->fogRate() };
  }
  else {
      _lighting_ubo_data.DiffuseColor_FogStart = { diffuse, 0 };
      _lighting_ubo_data.AmbientColor_FogEnd = { ambient, 0 };
      _lighting_ubo_data.LightDir_FogRate = { _outdoor_light_stats.dayDir.x, _outdoor_light_stats.dayDir.y, _outdoor_light_stats.dayDir.z, _skies->fogRate() };
  }
  _lighting_ubo_data.OceanColorLight = settings->ocean_color_light;
  _lighting_ubo_data.OceanColorDark = settings->ocean_color_dark;
  _lighting_ubo_data.RiverColorLight = settings->river_color_light;
  _lighting_ubo_data.RiverColorDark = settings->river_color_dark;

  gl.bindBuffer(GL_UNIFORM_BUFFER, _lighting_ubo);
  gl.bufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(OpenGL::LightingUniformBlock), &_lighting_ubo_data);
}

void WorldRender::updateTerrainParamsUniformBlock()
{
  ZoneScoped;
  gl.bindBuffer(GL_UNIFORM_BUFFER, _terrain_params_ubo);
  gl.bufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(OpenGL::TerrainParamsUniformBlock), &_terrain_params_ubo_data);
  _need_terrain_params_ubo_update = false;
}

void Noggit::Rendering::WorldRender::markTerrainParamsUniformBlockDirty()
{
  _need_terrain_params_ubo_update = true;
}

[[nodiscard]]
std::unique_ptr<Skies>& Noggit::Rendering::WorldRender::skies()
{
  return _skies;
}

float Noggit::Rendering::WorldRender::cullDistance() const
{
  return _cull_distance;
}

void WorldRender::setupChunkVAO(OpenGL::Scoped::use_program& mcnk_shader)
{
  ZoneScoped;
  OpenGL::Scoped::vao_binder const _ (_mapchunk_vao);

  {
    OpenGL::Scoped::buffer_binder<GL_ARRAY_BUFFER> const binder(_mapchunk_texcoord);
    mcnk_shader.attrib("texcoord", 2, GL_FLOAT, GL_FALSE, 0, 0);
  }

  {
    OpenGL::Scoped::buffer_binder<GL_ARRAY_BUFFER> const binder(_mapchunk_vertex);
    mcnk_shader.attrib("position", 2, GL_FLOAT, GL_FALSE, 0, 0);
  }
}

void WorldRender::setupChunkBuffers()
{
  ZoneScoped;

  // vertices

  glm::vec2 vertices[mapbufsize];
  glm::vec2 *ttv = vertices;

  for (int j = 0; j < 17; ++j)
  {
    bool is_lod = j % 2;
    for (int i = 0; i < (is_lod ? 8 : 9); ++i)
    {
      float xpos, zpos;
      xpos = i * UNITSIZE;
      zpos = j * 0.5f * UNITSIZE;

      if (is_lod)
      {
        xpos += UNITSIZE*0.5f;
      }

      auto v = glm::vec2(xpos, zpos);
      *ttv++ = v;
    }
  }

  gl.bufferData<GL_ARRAY_BUFFER>(_mapchunk_vertex, sizeof(vertices), vertices, GL_STATIC_DRAW);


  static constexpr std::array<std::uint16_t, 768 + 192> indices {

      9, 0, 17, 9, 17, 18, 9, 18, 1, 9, 1, 0, 26, 17, 34, 26,
      34, 35, 26, 35, 18, 26, 18, 17, 43, 34, 51, 43, 51, 52, 43, 52,
      35, 43, 35, 34, 60, 51, 68, 60, 68, 69, 60, 69, 52, 60, 52, 51,
      77, 68, 85, 77, 85, 86, 77, 86, 69, 77, 69, 68, 94, 85, 102, 94,
      102, 103, 94, 103, 86, 94, 86, 85, 111, 102, 119, 111, 119, 120, 111, 120,
      103, 111, 103, 102, 128, 119, 136, 128, 136, 137, 128, 137, 120, 128, 120, 119,
      10, 1, 18, 10, 18, 19, 10, 19, 2, 10, 2, 1, 27, 18, 35, 27,
      35, 36, 27, 36, 19, 27, 19, 18, 44, 35, 52, 44, 52, 53, 44, 53,
      36, 44, 36, 35, 61, 52, 69, 61, 69, 70, 61, 70, 53, 61, 53, 52,
      78, 69, 86, 78, 86, 87, 78, 87, 70, 78, 70, 69, 95, 86, 103, 95,
      103, 104, 95, 104, 87, 95, 87, 86, 112, 103, 120, 112, 120, 121, 112, 121,
      104, 112, 104, 103, 129, 120, 137, 129, 137, 138, 129, 138, 121, 129, 121, 120,
      11, 2, 19, 11, 19, 20, 11, 20, 3, 11, 3, 2, 28, 19, 36, 28,
      36, 37, 28, 37, 20, 28, 20, 19, 45, 36, 53, 45, 53, 54, 45, 54,
      37, 45, 37, 36, 62, 53, 70, 62, 70, 71, 62, 71, 54, 62, 54, 53,
      79, 70, 87, 79, 87, 88, 79, 88, 71, 79, 71, 70, 96, 87, 104, 96,
      104, 105, 96, 105, 88, 96, 88, 87, 113, 104, 121, 113, 121, 122, 113, 122,
      105, 113, 105, 104, 130, 121, 138, 130, 138, 139, 130, 139, 122, 130, 122, 121,
      12, 3, 20, 12, 20, 21, 12, 21, 4, 12, 4, 3, 29, 20, 37, 29,
      37, 38, 29, 38, 21, 29, 21, 20, 46, 37, 54, 46, 54, 55, 46, 55,
      38, 46, 38, 37, 63, 54, 71, 63, 71, 72, 63, 72, 55, 63, 55, 54,
      80, 71, 88, 80, 88, 89, 80, 89, 72, 80, 72, 71, 97, 88, 105, 97,
      105, 106, 97, 106, 89, 97, 89, 88, 114, 105, 122, 114, 122, 123, 114, 123,
      106, 114, 106, 105, 131, 122, 139, 131, 139, 140, 131, 140, 123, 131, 123, 122,
      13, 4, 21, 13, 21, 22, 13, 22, 5, 13, 5, 4, 30, 21, 38, 30,
      38, 39, 30, 39, 22, 30, 22, 21, 47, 38, 55, 47, 55, 56, 47, 56,
      39, 47, 39, 38, 64, 55, 72, 64, 72, 73, 64, 73, 56, 64, 56, 55,
      81, 72, 89, 81, 89, 90, 81, 90, 73, 81, 73, 72, 98, 89, 106, 98,
      106, 107, 98, 107, 90, 98, 90, 89, 115, 106, 123, 115, 123, 124, 115, 124,
      107, 115, 107, 106, 132, 123, 140, 132, 140, 141, 132, 141, 124, 132, 124, 123,
      14, 5, 22, 14, 22, 23, 14, 23, 6, 14, 6, 5, 31, 22, 39, 31,
      39, 40, 31, 40, 23, 31, 23, 22, 48, 39, 56, 48, 56, 57, 48, 57,
      40, 48, 40, 39, 65, 56, 73, 65, 73, 74, 65, 74, 57, 65, 57, 56,
      82, 73, 90, 82, 90, 91, 82, 91, 74, 82, 74, 73, 99, 90, 107, 99,
      107, 108, 99, 108, 91, 99, 91, 90, 116, 107, 124, 116, 124, 125, 116, 125,
      108, 116, 108, 107, 133, 124, 141, 133, 141, 142, 133, 142, 125, 133, 125, 124,
      15, 6, 23, 15, 23, 24, 15, 24, 7, 15, 7, 6, 32, 23, 40, 32,
      40, 41, 32, 41, 24, 32, 24, 23, 49, 40, 57, 49, 57, 58, 49, 58,
      41, 49, 41, 40, 66, 57, 74, 66, 74, 75, 66, 75, 58, 66, 58, 57,
      83, 74, 91, 83, 91, 92, 83, 92, 75, 83, 75, 74, 100, 91, 108, 100,
      108, 109, 100, 109, 92, 100, 92, 91, 117, 108, 125, 117, 125, 126, 117, 126,
      109, 117, 109, 108, 134, 125, 142, 134, 142, 143, 134, 143, 126, 134, 126, 125,
      16, 7, 24, 16, 24, 25, 16, 25, 8, 16, 8, 7, 33, 24, 41, 33,
      41, 42, 33, 42, 25, 33, 25, 24, 50, 41, 58, 50, 58, 59, 50, 59,
      42, 50, 42, 41, 67, 58, 75, 67, 75, 76, 67, 76, 59, 67, 59, 58,
      84, 75, 92, 84, 92, 93, 84, 93, 76, 84, 76, 75, 101, 92, 109, 101,
      109, 110, 101, 110, 93, 101, 93, 92, 118, 109, 126, 118, 126, 127, 118, 127,
      110, 118, 110, 109, 135, 126, 143, 135, 143, 144, 135, 144, 127, 135, 127, 126,

      // lod
      0, 34, 18, 18, 34, 36, 18, 36, 2, 18, 2, 0, 34, 68, 52, 52,
      68, 70, 52, 70, 36, 52, 36, 34, 68, 102, 86, 86, 102, 104, 86, 104,
      70, 86, 70, 68, 102, 136, 120, 120, 136, 138, 120, 138, 104, 120, 104, 102,
      2, 36, 20, 20, 36, 38, 20, 38, 4, 20, 4, 2, 36, 70, 54, 54,
      70, 72, 54, 72, 38, 54, 38, 36, 70, 104, 88, 88, 104, 106, 88, 106,
      72, 88, 72, 70, 104, 138, 122, 122, 138, 140, 122, 140, 106, 122, 106, 104,
      4, 38, 22, 22, 38, 40, 22, 40, 6, 22, 6, 4, 38, 72, 56, 56,
      72, 74, 56, 74, 40, 56, 40, 38, 72, 106, 90, 90, 106, 108, 90, 108,
      74, 90, 74, 72, 106, 140, 124, 124, 140, 142, 124, 142, 108, 124, 108, 106,
      6, 40, 24, 24, 40, 42, 24, 42, 8, 24, 8, 6, 40, 74, 58, 58,
      74, 76, 58, 76, 42, 58, 42, 40, 74, 108, 92, 92, 108, 110, 92, 110,
      76, 92, 76, 74, 108, 142, 126, 126, 142, 144, 126, 144, 110, 126, 110, 108};

  /*
  // indices
  std::uint16_t indices[768];
  int flat_index = 0;

  for (int x = 0; x<8; ++x)
  {
    for (int y = 0; y<8; ++y)
    {
      indices[flat_index++] = MapChunk::indexLoD(y, x); //9
      indices[flat_index++] = MapChunk::indexNoLoD(y, x); //0
      indices[flat_index++] = MapChunk::indexNoLoD(y + 1, x); //17
      indices[flat_index++] = MapChunk::indexLoD(y, x); //9
      indices[flat_index++] = MapChunk::indexNoLoD(y + 1, x); //17
      indices[flat_index++] = MapChunk::indexNoLoD(y + 1, x + 1); //18
      indices[flat_index++] = MapChunk::indexLoD(y, x); //9
      indices[flat_index++] = MapChunk::indexNoLoD(y + 1, x + 1); //18
      indices[flat_index++] = MapChunk::indexNoLoD(y, x + 1); //1
      indices[flat_index++] = MapChunk::indexLoD(y, x); //9
      indices[flat_index++] = MapChunk::indexNoLoD(y, x + 1); //1
      indices[flat_index++] = MapChunk::indexNoLoD(y, x); //0
    }
  }

   */

  {
    OpenGL::Scoped::buffer_binder<GL_ELEMENT_ARRAY_BUFFER> const _ (_mapchunk_index);
    gl.bufferData (GL_ELEMENT_ARRAY_BUFFER, (768 + 192) * sizeof(std::uint16_t), indices.data(), GL_STATIC_DRAW);
  }

  // tex coords
  glm::vec2 temp[mapbufsize], *vt;
  float tx, ty;

  // init texture coordinates for detail map:
  vt = temp;
  const float detail_half = 0.5f * detail_size / 8.0f;
  for (int j = 0; j < 17; ++j)
  {
    bool is_lod = j % 2;

    for (int i = 0; i< (is_lod ? 8 : 9); ++i)
    {
      tx = detail_size / 8.0f * i;
      ty = detail_size / 8.0f * j * 0.5f;

      if (is_lod)
        tx += detail_half;

      *vt++ = glm::vec2(tx, ty);
    }
  }

  gl.bufferData<GL_ARRAY_BUFFER> (_mapchunk_texcoord, sizeof(temp), temp, GL_STATIC_DRAW);

}

void WorldRender::setupLiquidChunkVAO(OpenGL::Scoped::use_program& water_shader)
{
  ZoneScoped;
  OpenGL::Scoped::vao_binder const _ (_liquid_chunk_vao);

  {
    OpenGL::Scoped::buffer_binder<GL_ARRAY_BUFFER> const binder(_liquid_chunk_vertex);
    water_shader.attrib("position", 2, GL_FLOAT, GL_FALSE, 0, 0);
  }
}

void WorldRender::setupLiquidChunkBuffers()
{
  ZoneScoped;

  // vertices
  glm::vec2 vertices[768 / 2];
  glm::vec2* vt = vertices;

  for (int z = 0; z < 8; ++z)
  {
    for (int x = 0; x < 8; ++x)
    {
      // first triangle
      *vt++ = glm::vec2(UNITSIZE * x, UNITSIZE * z);
      *vt++ = glm::vec2(UNITSIZE * x, UNITSIZE * (z + 1));
      *vt++ = glm::vec2(UNITSIZE * (x + 1), UNITSIZE * z);

      // second triangle
      *vt++ = glm::vec2(UNITSIZE * (x + 1), UNITSIZE * z);
      *vt++ = glm::vec2(UNITSIZE * x, UNITSIZE * (z + 1));
      *vt++ = glm::vec2(UNITSIZE * (x + 1), UNITSIZE * (z + 1));
    }
  }

  gl.bufferData<GL_ARRAY_BUFFER> (_liquid_chunk_vertex, sizeof(vertices), vertices, GL_STATIC_DRAW);

}



void WorldRender::setupOccluderBuffers()
{
  ZoneScoped;
  static constexpr std::array<std::uint16_t, 36> indices
      {
          /*Above ABC,BCD*/
          0,1,2,
          1,2,3,
          /*Following EFG,FGH*/
          4,5,6,
          5,6,7,
          /*Left ABF,AEF*/
          1,0,5,
          0,4,5,
          /*Right side CDH,CGH*/
          3,2,7,
          2,6,7,
          /*ACG,AEG*/
          2,0,6,
          0,4,6,
          /*Behind BFH,BDH*/
          5,1,7,
          1,3,7
      };

  {
    OpenGL::Scoped::buffer_binder<GL_ELEMENT_ARRAY_BUFFER> const _ (_occluder_index);
    gl.bufferData (GL_ELEMENT_ARRAY_BUFFER, 36 * sizeof(std::uint16_t), indices.data(), GL_STATIC_DRAW);
  }

}

void WorldRender::drawMinimap ( MapTile *tile
    , glm::mat4x4 const& model_view
    , glm::mat4x4 const& projection
    , glm::vec3 const& camera_pos
    , MinimapRenderSettings* settings
)
{
  ZoneScoped;

  // Also load a tile above the current one to correct the lookat approximation
  TileIndex m_tile = TileIndex(camera_pos);
  m_tile.z -= 1;

  bool unload = !_world->mapIndex.has_unsaved_changes(m_tile);

  MapTile* mTile = _world->mapIndex.loadTile(m_tile);

  if (mTile)
  {
    using clock_t = std::chrono::steady_clock;

    auto const start_wait = clock_t::now();
    mTile->wait_until_loaded();
    auto const end_wait = clock_t::now();
    auto const wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_wait - start_wait).count();
    if (wait_ms >= 1000)
    {
      LogError << "Minimap: wait_until_loaded tile=(" << mTile->index.x << "," << mTile->index.z << ") took "
               << wait_ms << " ms" << std::endl;
    }

    auto const start_children = clock_t::now();
    mTile->waitForChildrenLoaded();
    auto const end_children = clock_t::now();
    auto const children_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_children - start_children).count();
    if (children_ms >= 1000)
    {
      LogError << "Minimap: waitForChildrenLoaded tile=(" << mTile->index.x << "," << mTile->index.z << ") took "
               << children_ms << " ms" << std::endl;
    }

  }

  WorldRenderParams renderParams;

  renderParams.cursorRotation = 0.0f;
  renderParams.cursor_type = CursorType::NONE;
  renderParams.brush_radius = 0.f;
  renderParams.show_unpaintable_chunks = false;
  renderParams.draw_only_inside_light_sphere = false;
  renderParams.draw_wireframe_light_sphere = false;
  renderParams.alpha_light_sphere = false;
  renderParams.inner_radius_ratio = 0.3f;
  renderParams.angle = 0.0f;
  renderParams.orientation = 0.0f;
  renderParams.use_ref_pos = 0.0f;
  renderParams.angled_mode = 0.0f;
  renderParams.draw_paintability_overlay = false;
  renderParams.editing_mode = editing_mode::minimap;
  renderParams.camera_moved = true;
  renderParams.draw_mfbo = false;
  renderParams.draw_terrain = true;
  renderParams.draw_wmo = settings->draw_wmo;
  renderParams.draw_water = settings->draw_water;
  renderParams.draw_wmo_doodads = false;
  renderParams.draw_models = settings->draw_m2;
  renderParams.draw_model_animations = false;
  renderParams.draw_models_with_box = false;
  renderParams.draw_hidden_models = true;
  renderParams.draw_sky = false;
  renderParams.draw_skybox = false;
  renderParams.draw_fog = false;
  renderParams.ground_editing_brush = eTerrainType::eTerrainType_Linear;
  renderParams.water_layer = 0;
  renderParams.display_mode = display_mode::in_3D;
  renderParams.draw_occlusion_boxes = false;
  renderParams.minimap_render = true;
  renderParams.draw_wmo_exterior = true;
  renderParams.draw_chunk_manipulator_selection = false;

  draw(model_view, projection, glm::vec3(), glm::vec4(),
  glm::vec3(), camera_pos, settings, renderParams);


  if (unload)
  {
    _world->mapIndex.unloadTile(m_tile);
  }
}

bool WorldRender::saveMinimap(TileIndex const& tile_idx, MinimapRenderSettings* settings, std::optional<QImage>& combined_image)
{
  ZoneScoped;
  // Setup framebuffer
  QOpenGLFramebufferObjectFormat fmt;
  fmt.setSamples(0);
  fmt.setInternalTextureFormat(GL_RGBA8);
  fmt.setAttachment(QOpenGLFramebufferObject::Depth);

  QOpenGLFramebufferObject pixel_buffer(settings->resolution, settings->resolution, fmt);
  pixel_buffer.bind();

  gl.viewport(0, 0, settings->resolution, settings->resolution);
  gl.clearColor(.0f, .0f, .0f, 1.f);
  gl.clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Load tile
  bool unload = !_world->mapIndex.has_unsaved_changes(tile_idx);

  if (!_world->mapIndex.tileLoaded(tile_idx) && !_world->mapIndex.tileAwaitingLoading(tile_idx))
  {
    MapTile* tile = _world->mapIndex.loadTile(tile_idx);
    auto const start_wait = std::chrono::steady_clock::now();
    tile->wait_until_loaded();
    auto const end_wait = std::chrono::steady_clock::now();
    auto const wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_wait - start_wait).count();
    if (wait_ms >= 1000)
    {
      LogError << "Minimap: (save) wait_until_loaded tile=(" << tile_idx.x << "," << tile_idx.z << ") took "
               << wait_ms << " ms" << std::endl;
    }
    _world->wait_for_all_tile_updates();
    auto const start_children = std::chrono::steady_clock::now();
    tile->waitForChildrenLoaded();
    auto const end_children = std::chrono::steady_clock::now();
    auto const children_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_children - start_children).count();
    if (children_ms >= 1000)
    {
      LogError << "Minimap: (save) waitForChildrenLoaded tile=(" << tile_idx.x << "," << tile_idx.z << ") took "
               << children_ms << " ms" << std::endl;
    }
  }

  MapTile* mTile = _world->mapIndex.getTile(tile_idx);

  if (mTile)
  {
    unsigned counter = 0;
    constexpr unsigned TIMEOUT = 5000;

    while (AsyncLoader::instance->is_loading() || !mTile->finishedLoading())
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      counter++;

      if (counter >= TIMEOUT)
        break;
    }

    float max_height = std::max(_world->getMaxTileHeight(tile_idx), 200.f);

    // setup view matrices
    auto projection = glm::ortho( -TILESIZE / 2.0f,TILESIZE / 2.0f,-TILESIZE / 2.0f,TILESIZE / 2.0f,0.f,100000.0f);

    auto eye = glm::vec3(TILESIZE * tile_idx.x + TILESIZE / 2.0f, max_height + 10.0f, TILESIZE * tile_idx.z + TILESIZE / 2.0f);
    auto center = glm::vec3(TILESIZE * tile_idx.x + TILESIZE / 2.0f, max_height + 5.0f, TILESIZE * tile_idx.z + TILESIZE / 2.0 - 0.005f);
    auto up = glm::vec3(0.f, 1.f, 0.f);

    glm::vec3 const z = glm::normalize(eye - center);
    glm::vec3 const x = glm::normalize(glm::cross(up, z));
    glm::vec3 const y = glm::normalize(glm::cross(z, x));

    auto look_at = glm::transpose(glm::mat4x4(x.x, x.y, x.z, glm::dot(x, glm::vec3(-eye.x, -eye.y, -eye.z))
        , y.x, y.y, y.z, glm::dot(y, glm::vec3(-eye.x, -eye.y, -eye.z))
        , z.x, z.y, z.z, glm::dot(z, glm::vec3(-eye.x, -eye.y, -eye.z))
        , 0.f, 0.f, 0.f, 1.f
    ));

    glFinish();

    drawMinimap(mTile
        , look_at
        , projection
        , glm::vec3(TILESIZE * tile_idx.x + TILESIZE / 2.0f
            , max_height + 15.0f, TILESIZE * tile_idx.z + TILESIZE / 2.0f)
        , settings);

    // Clearing alpha from image
    gl.colorMask(false, false, false, true);
    gl.clearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gl.clear(GL_COLOR_BUFFER_BIT);
    gl.colorMask(true, true, true, true);

    assert(pixel_buffer.isValid() && pixel_buffer.isBound());

    QImage image = pixel_buffer.toImage();

    image = image.convertToFormat(QImage::Format_RGBA8888);

    QString str = QString(Noggit::Project::CurrentProject::get()->ProjectPath.c_str());
    if (!(str.endsWith('\\') || str.endsWith('/')))
    {
      str += "/";
    }

    QString target_dir = QString("/textures/minimap/");
    if(settings->export_mode == MinimapGenMode::LOD_MAPTEXTURES || settings->export_mode == MinimapGenMode::LOD_MAPTEXTURES_N)
	{
	  target_dir = QString("/textures/maptextures/");
	}

    QDir dir(str + target_dir);
    if (!dir.exists())
      dir.mkpath(".");

    std::string tex_name = std::string(_world->basename + "_" + std::to_string(tile_idx.x) + "_" + std::to_string(tile_idx.z) + ".blp");
    if (settings->export_mode == MinimapGenMode::LOD_MAPTEXTURES_N)
    {
        tex_name = std::string(_world->basename + "_" + std::to_string(tile_idx.x) + "_" + std::to_string(tile_idx.z) + "_n.blp");
    }

    if (settings->file_format == ".png")
    {
      image.save(dir.filePath(std::string(_world->basename + "_" + std::to_string(tile_idx.x) + "_" + std::to_string(tile_idx.z) + ".png").c_str()));
    }
    else if (settings->file_format == ".blp (DXT1)" || settings->file_format == ".blp (DXT5)")
    {
      QByteArray bytes;
      QBuffer buffer( &bytes );
      buffer.open( QIODevice::WriteOnly );

      image.save( &buffer, "PNG" );

      auto blp = Png2Blp();
      blp.load(reinterpret_cast<const void*>(bytes.constData()), bytes.size());

      uint32_t file_size;
      // void* blp_image = blp.createBlpDxtInMemory(true, FORMAT_DXT5, file_size);
      // this mirrors blizzards : dxt1, no mipmap
      void* blp_image = blp.createBlpDxtInMemory(settings->file_format == ".blp (DXT5)" ? true : false, settings->file_format == ".blp (DXT5)" ? FORMAT_DXT5 : FORMAT_DXT1, file_size);

      // converts the texture name to an md5 hash like blizzard, this is used to avoid duplicates textures for ocean
      // downside is that if the file gets updated regularly there will be a lot of duplicates in the project folder
      // probably should be a patching option when deploying
      bool use_md5 = false;
      if (use_md5)
      {
          QCryptographicHash md5_hash(QCryptographicHash::Md5);
          // auto data = reinterpret_cast<char*>(blp_image);
          md5_hash.addData(reinterpret_cast<char*>(blp_image), file_size);
          auto resulthex = md5_hash.result().toHex().toStdString() + ".blp";
          tex_name = resulthex;
      }


      QFile file(dir.filePath(tex_name.c_str()));
      file.open(QIODevice::WriteOnly);

      QDataStream out(&file);
      out.writeRawData(reinterpret_cast<char*>(blp_image), file_size);

      file.close();
    }

    // Write combined file
    if (settings->combined_minimap && combined_image.has_value())
    {
      QImage scaled_image = image.scaled(128, 128,  Qt::KeepAspectRatio);

      for (int i = 0; i < 128; ++i)
      {
        for (int j = 0; j < 128; ++j)
        {
          combined_image->setPixelColor(static_cast<int>(tile_idx.x) * 128 + j, static_cast<int>(tile_idx.z) * 128 + i, scaled_image.pixelColor(j, i));
        }
      }

    }

    // Register in md5translate.trs
    try
    {
        std::string map_name = gMapDB.getByID(_world->mapIndex._map_id).getString(MapDB::InternalName);
        auto sstream = std::stringstream();
        sstream << map_name << "\\map" << tile_idx.x << "_" << std::setfill('0') << std::setw(2) << tile_idx.z << ".blp";
        std::string tilename_left = sstream.str();
        auto& minimap_md5translate = Noggit::Application::NoggitApplication::instance()->clientData()->_minimap_md5translate;
        minimap_md5translate[map_name][tilename_left] = tex_name;
    }
    catch(MapDB::NotFound)
    {
        LogError << "SaveMinimap : Couldn't find entry " << _world->mapIndex._map_id << std::endl;
        assert(false);
    }

    if (unload)
    {
      _world->mapIndex.unloadTile(tile_idx);
    }

  }

  pixel_buffer.release();

  return true;
}

[[nodiscard]]
OpenGL::TerrainParamsUniformBlock* Noggit::Rendering::WorldRender::getTerrainParamsUniformBlock()
{
  return &_terrain_params_ubo_data;
}
