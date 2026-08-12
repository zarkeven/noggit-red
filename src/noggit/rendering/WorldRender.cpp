// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/rendering/WorldRender.hpp>
#include <noggit/MapHeaders.h>
#include <noggit/rendering/RealtimeGpuShadowMap.hpp>
#include <noggit/rendering/RealtimeSunDirection.hpp>
#include <noggit/errorHandling.h>
#include <noggit/Log.h>
#include <noggit/rendering/PointLightFlicker.hpp>
#include <external/PNG2BLP/Png2Blp.h>
#include <external/tracy/Tracy.hpp>
#include <math/frustum.hpp>
#include <noggit/application/Configuration/NoggitApplicationConfiguration.hpp>
#include <noggit/application/NoggitApplication.hpp>
#include <noggit/ModernLightTables.hpp>
#include <noggit/DBC.h>
#include <noggit/MapChunk.h>
#include <noggit/MapTile.h>
#include <noggit/TileIndex.hpp>
#include <noggit/MinimapRenderSettings.hpp>
#include <noggit/Misc.h>
#include <noggit/Model.h>
#include <noggit/ModelInstance.h>
#include <noggit/ModelManager.h>
#include <noggit/project/CurrentProject.hpp>
#include <noggit/WMOInstance.h>
#include <noggit/World.h>
#include <noggit/ui/tools/ChunkManipulator/ChunkClipboard.hpp>

#include <noggit/ui/MinimapCreator.hpp>

#include <opengl/scoped.hpp>
#include <opengl/shader.hpp>
#include <opengl/types.hpp>

#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QString>
#include <QStringList>
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
#include <limits>
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

  struct TexLayerBillboardInstance
  {
    glm::vec4 center_digit;
    glm::vec4 color;
  };

  static constexpr std::size_t kMaxTexLayerBillboardInstances = 24576u;

  struct saved_shadow_pass_gl_state
  {
    GLint viewport[4] {};
    GLboolean color_mask[4] {};
    GLboolean cull_face_enabled = GL_FALSE;
    GLboolean polygon_offset_fill_enabled = GL_FALSE;
    GLboolean depth_mask = GL_TRUE;
    GLint depth_func = GL_LEQUAL;
    GLint framebuffer = 0;

    saved_shadow_pass_gl_state()
    {
      gl.getIntegerv (GL_VIEWPORT, viewport);
      gl.getBooleanv (GL_COLOR_WRITEMASK, color_mask);
      cull_face_enabled = gl.isEnabled (GL_CULL_FACE);
      polygon_offset_fill_enabled = gl.isEnabled (GL_POLYGON_OFFSET_FILL);
      gl.getBooleanv (GL_DEPTH_WRITEMASK, &depth_mask);
      gl.getIntegerv (GL_DEPTH_FUNC, &depth_func);
      gl.getIntegerv (GL_FRAMEBUFFER_BINDING, &framebuffer);
    }

    ~saved_shadow_pass_gl_state()
    {
      gl.bindFramebuffer (GL_FRAMEBUFFER, static_cast<GLuint> (framebuffer));
      gl.viewport (viewport[0], viewport[1], viewport[2], viewport[3]);
      gl.colorMask (color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
      gl.depthMask (depth_mask);

      if (cull_face_enabled)
      {
        gl.enable (GL_CULL_FACE);
      }
      else
      {
        gl.disable (GL_CULL_FACE);
      }

      if (polygon_offset_fill_enabled)
      {
        gl.enable (GL_POLYGON_OFFSET_FILL);
      }
      else
      {
        gl.disable (GL_POLYGON_OFFSET_FILL);
      }

      gl.depthFunc (depth_func);
      gl.activeTexture (GL_TEXTURE0);
    }

    saved_shadow_pass_gl_state (saved_shadow_pass_gl_state const&) = delete;
    saved_shadow_pass_gl_state& operator= (saved_shadow_pass_gl_state const&) = delete;
  };

  static void restore_main_render_gl_state()
  {
    gl.bindFramebuffer (GL_FRAMEBUFFER, 0);
    gl.disable (GL_CULL_FACE);
    gl.disable (GL_POLYGON_OFFSET_FILL);
    gl.depthFunc (GL_LEQUAL);
    gl.colorMask (GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.depthMask (GL_TRUE);
    gl.activeTexture (GL_TEXTURE0);
  }

  static float distance_sq (glm::vec3 const& a, glm::vec3 const& b)
  {
    glm::vec3 const d = a - b;
    return glm::dot (d, d);
  }

  static float expand_shadow_ortho_for_casters (
    glm::vec3 const& camera_pos
  , float base_extent
  , tsl::robin_map<Model*, std::vector<glm::mat4x4>> const& models_to_draw
  , std::vector<WMOInstance*> const& wmos_to_draw
  )
  {
    float extent = base_extent;

    auto expand_point = [&] (glm::vec3 const& world_pos)
    {
      glm::vec2 const delta {
        world_pos.x - camera_pos.x
      , world_pos.z - camera_pos.z
      };
      extent = std::max (extent, std::max (std::abs (delta.x), std::abs (delta.y)) + 128.f);
    };

    for (WMOInstance* inst : wmos_to_draw)
    {
      if (!inst)
      {
        continue;
      }

      std::array<glm::vec3, 2> const& ext = inst->getExtents();
      for (int ix = 0; ix < 2; ++ix)
      {
        for (int iy = 0; iy < 2; ++iy)
        {
          for (int iz = 0; iz < 2; ++iz)
          {
            expand_point ({
              ix ? ext[1].x : ext[0].x
            , iy ? ext[1].y : ext[0].y
            , iz ? ext[1].z : ext[0].z
            });
          }
        }
      }
    }

    for (auto const& pair : models_to_draw)
    {
      if (!pair.first)
      {
        continue;
      }

      glm::vec3 const bb_min = misc::transform_model_box_coords (pair.first->bounding_box_min);
      glm::vec3 const bb_max = misc::transform_model_box_coords (pair.first->bounding_box_max);

      for (glm::mat4x4 const& inst : pair.second)
      {
        for (int ix = 0; ix < 2; ++ix)
        {
          for (int iy = 0; iy < 2; ++iy)
          {
            for (int iz = 0; iz < 2; ++iz)
            {
              glm::vec3 const local {
                ix ? bb_max.x : bb_min.x
              , iy ? bb_max.y : bb_min.y
              , iz ? bb_max.z : bb_min.z
              };
              expand_point (glm::vec3 (inst * glm::vec4 (local, 1.f)));
            }
          }
        }
      }
    }

    return std::min (extent, 1200.f);
  }

  static bool can_draw_gpu_sun_shadows (WorldRenderParams const& render_settings)
  {
    if (render_settings.minimap_render)
    {
      return false;
    }

    if (render_settings.display_mode != display_mode::in_3D)
    {
      return false;
    }

    return render_settings.draw_models || render_settings.draw_wmo;
  }

  static void bind_gpu_sun_shadow ( OpenGL::Scoped::use_program& shader
                                 , WorldRenderParams const& render_settings
                                 , Noggit::Rendering::RealtimeGpuShadowMap const& shadow_map
                                 )
  {
    constexpr int k_shadow_tex_unit = 17;

    shader.uniform("realtime_shadows_enabled", 0);
    shader.uniform("shadow_darkness", 1.f);
    shader.uniform("sun_shadow_matrix", glm::mat4(1.f));
    shader.uniform("sun_shadow_light_dir", glm::vec3(0.f, -1.f, 0.f));
    shader.uniform("sun_shadow_depth", k_shadow_tex_unit);

    gl.activeTexture(GL_TEXTURE0 + k_shadow_tex_unit);
    gl.bindTexture(GL_TEXTURE_2D, 0);
    gl.activeTexture(GL_TEXTURE0);

    if (!render_settings.draw_realtime_shadows || !shadow_map.ready())
    {
      return;
    }

    shader.uniform("realtime_shadows_enabled", 1);
    shader.uniform("shadow_darkness", 0.45f);
    shader.uniform("sun_shadow_matrix", shadow_map.matrices().view_proj_bias);
    shader.uniform("sun_shadow_light_dir", shadow_map.light_travel_direction());
    gl.activeTexture(GL_TEXTURE0 + k_shadow_tex_unit);
    gl.bindTexture(GL_TEXTURE_2D, shadow_map.depth_texture());
    gl.activeTexture(GL_TEXTURE0);
  }

  static glm::vec4 tex_layer_digit_color(int n)
  {
    n = std::clamp(n, 0, 4);
    glm::vec4 c(0.62f, 0.62f, 0.64f, 1.f);
    if (n == 1)
    {
      c = glm::vec4(108.f / 255.f, 151.f / 255.f, 240.f / 255.f, 1.f);
    }
    else if (n == 2)
    {
      c = glm::vec4(100.f / 255.f, 245.f / 255.f, 101.f / 255.f, 1.f);
    }
    else if (n == 3)
    {
      c = glm::vec4(245.f / 255.f, 166.f / 255.f, 66.f / 255.f, 1.f);
    }
    else if (n == 4)
    {
      c = glm::vec4(245.f / 255.f, 53.f / 255.f, 50.f / 255.f, 1.f);
    }
    return c;
  }

  static QImage build_texture_layer_digit_atlas_image()
  {
    constexpr int kDigits = 5;
    constexpr int kCellH = 176;
    constexpr int kCellW = 132;

    QImage img(kCellW * kDigits, kCellH, QImage::Format_RGBA8888);
    img.fill(0x00000000);

    QString const paths[] = {
      QStringLiteral("C:/Users/donal/Downloads/ARIALNB.TTF"),
      QDir::homePath() + QStringLiteral("/Downloads/ARIALNB.TTF"),
    };

    QFont font;
    bool loaded = false;
    for (QString const& path : paths)
    {
      if (!QFile::exists(path))
      {
        continue;
      }

      int const id = QFontDatabase::addApplicationFont(path);
      if (id < 0)
      {
        continue;
      }

      QStringList const fams = QFontDatabase::applicationFontFamilies(id);
      if (fams.empty())
      {
        continue;
      }

      font = QFont(fams.front());
      loaded = true;
      break;
    }

    if (!loaded)
    {
      font = QFont(QStringLiteral("Arial"), 96, QFont::Bold);
      font.setStretch(80);
    }

    font.setPixelSize(118);
    font.setStyleHint(QFont::SansSerif, QFont::PreferAntialias);

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    for (int d = 0; d < kDigits; ++d)
    {
      QRect const cell(d * kCellW, 0, kCellW, kCellH);
      QString const s = QString::number(d);

      QPainterPath path;
      path.addText(0.f, 0.f, font, s);
      QRectF const br = path.boundingRect();
      QPointF const delta(cell.center().x() - br.center().x(), cell.center().y() - br.center().y());
      path.translate(delta);

      painter.strokePath(path, QPen(QColor(0, 0, 0, 245), 5.f, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.fillPath(path, QColor(255, 255, 255, 255));
    }

    painter.end();

    // OpenGL convention: first row is bottom of texture.
    return img.mirrored(false, true);
  }

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

namespace
{
  //! Shader fog still uses Skies::fog_distance_end(); this only relaxes ADT/M2/WMO culling when fog is on.
  float draw_cull_distance_with_fog(float view_distance, Skies const& skies)
  {
    float const fog_end = skies.fog_distance_end();
    constexpr float kFogCullLift = 1.2f;
    constexpr float kMinViewFractionWhenFog = 0.88f;
    return std::max(fog_end * kFogCullLift, view_distance * kMinViewFractionWhenFog);
  }

}

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

  gl.bindBuffer(GL_UNIFORM_BUFFER, _mvp_ubo);
  gl.bufferData(GL_UNIFORM_BUFFER, sizeof(OpenGL::MVPUniformBlock), NULL, GL_DYNAMIC_DRAW);
  gl.bindBufferRange(GL_UNIFORM_BUFFER, OpenGL::ubo_targets::MVP, _mvp_ubo, 0, sizeof(OpenGL::MVPUniformBlock));
  gl.bindBuffer(GL_UNIFORM_BUFFER, 0);

  gl.bindBuffer(GL_UNIFORM_BUFFER, _lighting_ubo);
  gl.bufferData(GL_UNIFORM_BUFFER, sizeof(OpenGL::LightingUniformBlock), NULL, GL_DYNAMIC_DRAW);
  gl.bindBufferRange(GL_UNIFORM_BUFFER, OpenGL::ubo_targets::LIGHTING, _lighting_ubo, 0, sizeof(OpenGL::LightingUniformBlock));
  gl.bindBuffer(GL_UNIFORM_BUFFER, 0);

  gl.bindBuffer(GL_UNIFORM_BUFFER, _point_lights_ubo);
  gl.bufferData(GL_UNIFORM_BUFFER, sizeof(OpenGL::PointLightsUniformBlock), NULL, GL_DYNAMIC_DRAW);
  gl.bindBufferRange(GL_UNIFORM_BUFFER, OpenGL::ubo_targets::POINT_LIGHTS, _point_lights_ubo, 0, sizeof(OpenGL::PointLightsUniformBlock));
  gl.bindBuffer(GL_UNIFORM_BUFFER, 0);

  gl.bindBuffer(GL_UNIFORM_BUFFER, _modern_fog_ubo);
  gl.bufferData(GL_UNIFORM_BUFFER, sizeof(OpenGL::ModernFogUniformBlock), NULL, GL_DYNAMIC_DRAW);
  gl.bindBufferRange(GL_UNIFORM_BUFFER, OpenGL::ubo_targets::MODERN_FOG, _modern_fog_ubo, 0, sizeof(OpenGL::ModernFogUniformBlock));
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
    mcnk_shader.bind_uniform_block("modern_fog", 6);

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

  _sea_level_clip_program.reset(
    new OpenGL::program{
      { GL_VERTEX_SHADER, OpenGL::shader::src_from_qrc("sea_level_clip_vs") },
      { GL_FRAGMENT_SHADER, OpenGL::shader::src_from_qrc("sea_level_clip_fs") },
    });

  gl.genVertexArrays(1, &_sea_level_clip_vao);
  gl.genBuffers(1, &_sea_level_clip_vbo);
  {
    std::array<glm::vec3, 6> init{};
    OpenGL::Scoped::use_program sea_sh(*_sea_level_clip_program.get());
    OpenGL::Scoped::vao_binder const sea_vao_bind(_sea_level_clip_vao);
    OpenGL::Scoped::buffer_binder<GL_ARRAY_BUFFER> sea_vbuf(_sea_level_clip_vbo);
    gl.bufferData(GL_ARRAY_BUFFER, sizeof(init), init.data(), GL_DYNAMIC_DRAW);
    sea_sh.attrib("world_position", 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), 0);
  }

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
    m2_shader.bind_uniform_block("modern_fog", 6);
  }

  {
    std::vector<int> samplers {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    OpenGL::Scoped::use_program wmo_program {*_wmo_program.get()};
    wmo_program.uniform("render_batches_tex", 0);
    wmo_program.uniform("texture_samplers", samplers);
    wmo_program.bind_uniform_block("lighting", 1);
    wmo_program.bind_uniform_block("point_lights", 5);
    wmo_program.bind_uniform_block("modern_fog", 6);
  }

  {
    OpenGL::Scoped::use_program m2_shader_instanced {*_m2_instanced_program.get()};
    m2_shader_instanced.bind_uniform_block("lighting", 1);
    m2_shader_instanced.bind_uniform_block("point_lights", 5);
    m2_shader_instanced.bind_uniform_block("modern_fog", 6);
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

    liquid_render.bind_uniform_block("matrices", 0);
    liquid_render.bind_uniform_block("lighting", 1);
    liquid_render.bind_uniform_block("liquid_layers_params", 4);
    liquid_render.uniform("vertex_data", 0);
    liquid_render.uniform("texture_samplers", liquid_samplers);
  }

  setupMccvVizBuffers();
  setupTextureLayerBillboardResources();
  setupSoundEmitterBillboardResources();

  _sun_shadow_m2_program.reset(
    new OpenGL::program{
      { GL_VERTEX_SHADER, OpenGL::shader::src_from_qrc("m2_vs", {"instanced"}) },
      { GL_FRAGMENT_SHADER, OpenGL::shader::src_from_qrc("m2_fs", {"M2_SHADOW_DEPTH_PASS"}) },
    });

  _sun_shadow_wmo_program.reset(
    new OpenGL::program{
      { GL_VERTEX_SHADER, OpenGL::shader::src_from_qrc("sun_shadow_wmo_depth_vs") },
      { GL_FRAGMENT_SHADER, OpenGL::shader::src_from_qrc("sun_shadow_wmo_depth_fs") },
    });

  {
    OpenGL::Scoped::use_program m2_shadow_shader { *_sun_shadow_m2_program.get() };
    m2_shadow_shader.uniform("bone_matrices", 0);
    m2_shadow_shader.uniform("tex1", 1);
    m2_shadow_shader.uniform("tex2", 2);
    m2_shadow_shader.uniform("terrain_uv_mask", 0);
  }

  {
    OpenGL::Scoped::use_program wmo_shadow_shader { *_sun_shadow_wmo_program.get() };
  }
}

void WorldRender::invalidateRealtimeShadows()
{
  _gpu_sun_shadow.invalidate();
}

void WorldRender::drawSunShadowDepthPass ( glm::vec3 const& camera_pos
                                         , glm::vec3 const& sun_dir
                                         , float shadow_distance
                                         , math::frustum const& frustum
                                         , WorldRenderParams const& render_settings
                                         , tsl::robin_map<Model*, std::vector<glm::mat4x4>> const& models_to_draw
                                         , std::vector<WMOInstance*> const& wmos_to_draw
                                         )
{
  ZoneScopedN ("WorldRender::drawSunShadowDepthPass");

  if (!can_draw_gpu_sun_shadows (render_settings))
  {
    return;
  }

  saved_shadow_pass_gl_state gl_state_guard;
  (void) gl_state_guard;

  Noggit::Rendering::RealtimeGpuShadowMap::ScopedDepthPass const depth_pass { _gpu_sun_shadow };

  glm::mat4x4 const& light_view = _gpu_sun_shadow.matrices().view;
  glm::mat4x4 const& light_projection = _gpu_sun_shadow.matrices().projection;

  if (render_settings.draw_wmo && _sun_shadow_wmo_program)
  {
    OpenGL::Scoped::use_program wmo_shader { *_sun_shadow_wmo_program.get() };
    OpenGL::Scoped::bool_setter<GL_BLEND, GL_FALSE> const blend_off;
    OpenGL::Scoped::depth_mask_setter<GL_TRUE> const depth_write_on;

    wmo_shader.uniform ("model_view", light_view);
    wmo_shader.uniform ("projection", light_projection);

    for (WMOInstance* instance : wmos_to_draw)
    {
      if (!instance)
      {
        continue;
      }

      try
      {
        instance->draw (
          wmo_shader
        , light_view
        , light_projection
        , frustum
        , _cull_distance
        , camera_pos
        , false
        , false
        , false
        , false
        , _world->animtime
        , false
        , render_settings.display_mode
        , true
        , true
        , false
        , false
        , true
        );
      }
      catch (...)
      {
        continue;
      }
    }
  }

  if (render_settings.draw_models && _sun_shadow_m2_program)
  {
    OpenGL::Scoped::use_program m2_shader { *_sun_shadow_m2_program.get() };
    OpenGL::Scoped::bool_setter<GL_BLEND, GL_FALSE> const blend_off;
    OpenGL::Scoped::depth_mask_setter<GL_TRUE> const depth_write_on;

    OpenGL::M2RenderState model_render_state;
    m2_shader.uniform ("model_view", light_view);
    m2_shader.uniform ("projection", light_projection);
    m2_shader.uniform ("anim_bones", render_settings.draw_model_animations ? 1 : 0);
    m2_shader.uniform ("tex_unit_lookup_1", 0);
    m2_shader.uniform ("tex_unit_lookup_2", 0);
    m2_shader.uniform ("terrain_uv_mask", 0);

    std::unordered_map<Model*, std::size_t> empty_boxes;

    for (auto const& pair : models_to_draw)
    {
      Model* model = pair.first;
      if (!model)
      {
        continue;
      }

      try
      {
        model->renderer()->draw (
          light_view
        , pair.second
        , m2_shader
        , model_render_state
        , frustum
        , _cull_distance
        , camera_pos
        , _world->animtime
        , false
        , empty_boxes
        , render_settings.display_mode
        , false
        , true
        , false
        , false
        , nullptr
        , false
        , true
        );
      }
      catch (...)
      {
        continue;
      }
    }
  }

  restore_main_render_gl_state();
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

  Noggit::register_crash_render_stage("WorldRender::draw:enter");

  glm::mat4x4 const mvp(projection * model_view);
  math::frustum const frustum (mvp);

  if (render_settings.camera_moved)
    updateMVPUniformBlock(model_view, projection);

  gl.disable(GL_DEPTH_TEST);

  Noggit::register_crash_render_stage("WorldRender::draw:lighting");
  if (!render_settings.minimap_render)
  {
    int daytime = static_cast<int>(_world->time) % 2880;
    // Always blend local light zones when drawing distance fog so start/end/color
    // match the fog settings at the camera (not only the map's global light).
    bool render_local_lightning = render_settings.editing_mode == editing_mode::light
      || local_lightning
      || render_settings.draw_fog;
    _skies->update_sky_colors(camera_pos, daytime, !render_local_lightning);
    // Refresh every frame so the fog toggle and camera-zone fog params apply immediately
    // (sky recalc can early-out for small camera moves).
    updateLightingUniformBlock(render_settings.draw_fog, camera_pos);
    updateModernFogUniformBlock(render_settings.draw_fog, render_settings.draw_volumetric_fog
                              , camera_pos, render_settings.camera_moved);
    updatePointLightsUniformBlock(render_settings.draw_point_lights, camera_pos, render_settings.camera_moved);
  }
  else
  {
    updateLightingUniformBlockMinimap(minimap_render_settings);
  }

  Noggit::register_crash_render_stage("WorldRender::draw:params");
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
  Noggit::register_crash_render_stage("WorldRender::draw:frustum");
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
  Noggit::register_crash_render_stage("WorldRender::draw:sky");
  if(!render_settings.minimap_render && render_settings.display_mode == display_mode::in_3D && render_settings.draw_sky
     && _m2_program)
  {
    ZoneScopedN("World::draw() : Draw skies");
    OpenGL::Scoped::use_program m2_shader {*_m2_program.get()};
    m2_shader.uniform("model_view", model_view);
    m2_shader.uniform("projection", projection);
    bind_gpu_sun_shadow(m2_shader, render_settings, _gpu_sun_shadow);
    m2_shader.uniform("realtime_shadows_enabled", 0);

    bool hadSky = false;

    if (render_settings.draw_wmo || _world->mapIndex.hasAGlobalWMO())
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

  gl.enable(GL_BLEND);
  gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  _cull_distance = render_settings.draw_fog && _skies
    ? draw_cull_distance_with_fog(_view_distance, *_skies)
    : _view_distance;

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

  if (modern_features && render_settings.draw_volumetric_fog && !render_settings.minimap_render
      && (render_settings.editing_mode == editing_mode::light
          || render_settings.editing_mode == editing_mode::point_light))
  {
    drawVolumetricFogDebug(model_view, projection, camera_pos, _cull_distance);
  }

  gl.enable(GL_DEPTH_TEST);
  gl.depthFunc(GL_LEQUAL); // less z-fighting artifacts this way, I think
  //gl.disable(GL_BLEND);
  gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  //gl.disable(GL_CULL_FACE);

  _world->_n_rendered_tiles = 0;
  _world->_n_rendered_objects = 0;

  tsl::robin_map<Model*, std::vector<glm::mat4x4>> models_to_draw;
  std::vector<WMOInstance*> wmos_to_draw;
  std::unordered_map<Model*, std::size_t> model_boxes_to_draw;

  static int object_draw_frame = 0;
  if (object_draw_frame == std::numeric_limits<int>::max())
  {
    object_draw_frame = 0;
  }
  else
  {
    ++object_draw_frame;
  }

  collectVisibleObjects (
    object_draw_frame
  , model_view
  , camera_pos
  , frustum
  , render_settings
  , minimap_render_settings
  , models_to_draw
  , wmos_to_draw
  );

  glm::vec3 const sun_dir = directional_lightning
    ? wow_directional_light_toward_sun (glm::vec3 (
        _lighting_ubo_data.LightDir_FogRate.x
      , _lighting_ubo_data.LightDir_FogRate.y
      , _lighting_ubo_data.LightDir_FogRate.z
      ))
    : editor_realtime_shadow_sun();

  if (render_settings.draw_realtime_shadows && can_draw_gpu_sun_shadows (render_settings))
  {
    float const max_shadow_dist = std::min (
      std::max (700.f, _view_distance * 0.5f)
    , 1100.f
    );
    float const base_ortho_extent = std::min (std::max (max_shadow_dist * 0.5f, 320.f), 800.f);
    float const ortho_extent = expand_shadow_ortho_for_casters (
      camera_pos
    , base_ortho_extent
    , models_to_draw
    , wmos_to_draw
    );
    _gpu_sun_shadow.prepare_frame (
      camera_pos
    , sun_dir
    , ortho_extent
    , max_shadow_dist * 1.15f
    );

    // Rebake every frame: casters (M2/WMO) move independently of the camera texel snap heuristic.
    drawSunShadowDepthPass (
      camera_pos
    , sun_dir
    , max_shadow_dist
    , frustum
    , render_settings
    , models_to_draw
    , wmos_to_draw
    );

    restore_main_render_gl_state();
  }

  if (render_settings.draw_terrain && _mcnk_program)
  {
    ZoneScopedN("World::draw() : Draw terrain");

    gl.disable(GL_BLEND);

    {
      OpenGL::Scoped::use_program mcnk_shader{ *_mcnk_program.get() };
      mcnk_shader.uniform("model_view", model_view);
      mcnk_shader.uniform("projection", projection);

      mcnk_shader.uniform("enable_mists_heightmapping", modern_features);
      bind_gpu_sun_shadow(mcnk_shader, render_settings, _gpu_sun_shadow);
      mcnk_shader.uniform("albedo_only", 0);
      mcnk_shader.uniform("preview_pass", 0);
      mcnk_shader.uniform("preview_alpha", 1.0f);
      mcnk_shader.uniform("camera", glm::vec3(camera_pos.x, camera_pos.y, camera_pos.z));
      mcnk_shader.uniform("animtime", static_cast<int>(_world->animtime));

      if (render_settings.cursor_type != CursorType::NONE)
      {
        int draw_cursor_circle = 0;
        if (render_settings.cursor_type == CursorType::STAMP)
        {
          draw_cursor_circle = 2;
        }
        else if (render_settings.cursor_type == CursorType::CIRCLE)
        {
          switch (render_settings.brush_cursor_style)
          {
            case BrushCursorStyle::TerrainWrap:
              draw_cursor_circle = 1;
              break;
            case BrushCursorStyle::DottedOutline:
              draw_cursor_circle = 3;
              break;
            default:
              draw_cursor_circle = 0;
              break;
          }
        }

        mcnk_shader.uniform("draw_cursor_circle", draw_cursor_circle);
        mcnk_shader.uniform("cursor_position", glm::vec3(cursor_pos.x, cursor_pos.y, cursor_pos.z));
        mcnk_shader.uniform("cursorRotation", render_settings.cursorRotation);
        mcnk_shader.uniform("outer_cursor_radius", render_settings.brush_radius);
        mcnk_shader.uniform("inner_cursor_ratio", render_settings.inner_radius_ratio);
        mcnk_shader.uniform("cursor_color", cursor_color);
        mcnk_shader.uniform("inner_cursor_color", render_settings.inner_cursor_outline_color);
        mcnk_shader.uniform("outer_cursor_color", render_settings.outer_cursor_outline_color);
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

        if (tile->loading_failed())
        {
          continue;
        }

        if (render_settings.minimap_render)
          tile->renderer()->setOccluded(false);

        // Terrain must not be skipped by tile occlusion queries. Culled tiles do not write depth, so
        // GL_ANY_SAMPLES_PASSED stays 0 and the ADT can vanish until the camera enters its bounds.

        // skipping unfinished adts really improves performance so we don't have to reuplaod them every frame
        if (!tile->texturesFinishedLoading())
          continue;

        // Limit rate uploading alphamap data to avoid long frame times (causes freezes)
        // Skip uploads entirely while the camera is moving — stutter-free navigation.
        unsigned int const max_chunk_updates = render_settings.camera_moved ? 0u : _frame_max_chunk_updates;
        bool skip_updates = false;
        if (num_chunks_uploaded_alphamap > max_chunk_updates)
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

        // Chunk Manipulator live paste preview: draw ghost terrain using cached height rows (30% opacity).
        if (render_settings.draw_chunk_manipulator_selection && !render_settings.minimap_render
            && render_settings.display_mode == display_mode::in_3D)
        {
          auto it_rows = chunk_manip_prev_height_rows.find(tile->index);
          if (it_rows != chunk_manip_prev_height_rows.end() && !it_rows->second.empty())
          {
            OpenGL::Scoped::bool_setter<GL_BLEND, GL_TRUE> const blend_on;
            gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            tile->renderer()->updateChunkManipulatorPreviewHeightmap(it_rows->second);
            tile->renderer()->drawChunkManipulatorPreview(mcnk_shader, 0.3f);
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

    drawBrushCursorOverlay(mvp, cursor_pos, cursor_color, render_settings);
    drawMccvVertexAltViz(model_view, projection, mvp, camera_pos, cursor_color, render_settings);
    drawTextureLayerCountBillboards(model_view, projection, camera_pos, frustum, render_settings);
    drawRampPreview(mvp, render_settings);
  }

  // Sea level as soon as terrain (and terrain-adjacent overlays) have written the depth buffer.
  // Drawing after WMO/M2 lets those occlude the plane and can make shoreline depth alternate terrain vs
  // nearer geometry every frame → visible flicker. ADT water still draws after this block.
  if (_terrain_params_ubo_data.draw_sea_level_plane && !render_settings.minimap_render
      && render_settings.display_mode == display_mode::in_3D)
  {
    ZoneScopedN("World::draw() : Draw sea level plane");

    gl.enable(GL_BLEND);
    gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    OpenGL::Scoped::depth_mask_setter<GL_FALSE> const depth_mask;

    float const plane_radius = std::max(_view_distance * 25.f, 12000.f);
    glm::vec4 const sea_color(46.f / 255.f, 107.f / 255.f, 199.f / 255.f, 0.4f);

    drawSeaLevelPlane(model_view, projection, camera_pos, plane_radius, sea_color);
  }

  // Terrain base-color lookup texture for WMO shader-16 blending (modern clients).
  // Render top-down albedo into an FBO around the camera, then sample it in the WMO shader.
  {
    QSettings settings;
    bool const wmo_terrain_blend = settings.value("wmo_terrain_blend", false).toBool();

    if (wmo_terrain_blend && modern_features && render_settings.draw_terrain && render_settings.draw_wmo && _mcnk_program)
    {
      glm::vec2 const center_xz(camera_pos.x, camera_pos.z);
      float const move_thresh = _terrain_blend_world_size * 0.4f;
      bool const center_shifted =
        glm::distance(center_xz, _terrain_blend_last_center_xz) > move_thresh || _terrain_blend_fbo == nullptr;

      if (center_shifted)
      {
        _terrain_blend_rebake_pending = true;
        _terrain_blend_pending_center_xz = center_xz;
      }

      // Rebaking draws the full terrain again into an FBO; defer until movement stops so fly/walk stays smooth.
      bool const should_rebake = _terrain_blend_rebake_pending
                              && (!render_settings.camera_moved || _terrain_blend_fbo == nullptr);

      if (should_rebake)
      {
        glm::vec2 const bake_center = _terrain_blend_pending_center_xz;
        _terrain_blend_rebake_pending = false;
        _terrain_blend_last_center_xz = bake_center;
        _terrain_blend_origin_xz = bake_center - glm::vec2(_terrain_blend_world_size * 0.5f);
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
        glm::vec3 const eye(bake_center.x, camera_pos.y + 4096.f, bake_center.y);
        glm::vec3 const target(bake_center.x, 0.f, bake_center.y);
        glm::mat4 const bake_view = glm::lookAt(eye, target, glm::vec3(0.f, 0.f, -1.f));
        float const half = _terrain_blend_world_size * 0.5f;
        glm::mat4 const bake_proj = glm::ortho(-half, half, -half, half, 1.f, 16384.f);
        math::frustum const bake_frustum (bake_proj * bake_view);

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

          auto const tile_extents = tile->getTerrainWaterCullExtents();
          if (!bake_frustum.intersects(tile_extents[1], tile_extents[0]))
            continue;

          tile->renderer()->setOccluded(false);
          tile->renderer()->draw(mcnk_shader, glm::vec3(bake_center.x, camera_pos.y, bake_center.y),
                                 /*show_unpaintable_chunks*/ false,
                                 /*draw_paintability_overlay*/ false,
                                 /*is_selected*/ false,
                                 /*skip_upload_alphamap*/ true);
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
      bool const wmo_terrain_blend = settings.value("wmo_terrain_blend", false).toBool();
      wmo_program.uniform("wmo_terrain_blend_enabled", static_cast<int>(wmo_terrain_blend && _terrain_blend_color_tex != 0));
      wmo_program.uniform("terrain_blend_origin_xz", _terrain_blend_origin_xz);
      wmo_program.uniform("terrain_blend_inv_size", _terrain_blend_inv_size);
      wmo_program.uniform("terrain_blend_color", 16);

      gl.activeTexture(GL_TEXTURE0 + 16);
      gl.bindTexture(GL_TEXTURE_2D, wmo_terrain_blend ? _terrain_blend_color_tex : 0);

      bind_gpu_sun_shadow(wmo_program, render_settings, _gpu_sun_shadow);

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
          try
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
          catch (std::exception const& e)
          {
            // One corrupt/oversized WMO must not abort the frame (blank map).
            LogError << "WMO draw failed for uid " << instance->uid << ": " << e.what() << std::endl;
            try
            {
              instance->wmo->error_on_loading();
            }
            catch (...)
            {
            }
          }
        }
      }
    }
  }


  // Occlusion culling: terrain always draws first (depth buffer), then queries test each tile AABB
  // against that depth. M2/WMO/water on occluded tiles are skipped. Terrain is never skipped.
  // occlusion latency has 1-2 frames delay.

  constexpr bool occlusion_cull = true;
  if (occlusion_cull && _occluder_program && !render_settings.camera_moved)
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

        // Do not mark tiles occluded if terrain did not draw last frame (stale query / feedback loop).
        if (!tile->_was_rendered_last_frame)
        {
          tile->renderer()->setOccluded(false);
        }
        else
        {
          tile->renderer()->setOccluded(!tile->renderer()->getTileOcclusionQueryResult(camera_pos));
        }
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
        bind_gpu_sun_shadow(m2_shader, render_settings, _gpu_sun_shadow);

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

  // set anim time only once per frame
  if (_liquid_program)
  {
    OpenGL::Scoped::use_program water_shader {*_liquid_program.get()};
    water_shader.uniform("camera", glm::vec3(camera_pos.x, camera_pos.y, camera_pos.z));
    water_shader.uniform("animtime", _world->animtime);

    if (render_settings.draw_wmo || _world->mapIndex.hasAGlobalWMO())
    {
      water_shader.uniform("use_transform", 1);
    }
  }

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

  if (render_settings.draw_water)
  {
    ZoneScopedN("World::draw() : Draw water");
    Noggit::register_crash_render_stage("WorldRender::draw:water");

    // draw the water on both sides
    OpenGL::Scoped::bool_setter<GL_CULL_FACE, GL_FALSE> const cull;

    OpenGL::Scoped::use_program water_shader{ *_liquid_program.get()};

    gl.bindVertexArray(_liquid_chunk_vao);

    water_shader.uniform("use_transform", 0);

    for (auto& pair : _world->_loaded_tiles_buffer)
    {
      MapTile* tile = pair.second;

      if (!tile)
        break;

      if (!tile->texturesFinishedLoading())
        continue;

      if (!tile->Water.isVisible(frustum) && !tile->Water.needsUpdate())
        continue;

      // Match terrain: do not skip liquid on occlusion — it was cutting water off mid-view
      // and is unrelated to whether the tile's terrain mesh is hidden.
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
    Noggit::register_crash_render_stage("WorldRender::draw:water_done");
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
      if (!light)
        continue;

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
    std::vector<glm::vec3> cone_lines;
    cone_lines.reserve(128);

    for (std::size_t li = 0; li < _world->_point_lights.size(); ++li)
    {
      auto const& light = _world->_point_lights[li];

      float const viz_radius = std::max({ k_point_light_sphere_radius
                                        , light.attenuation_end
                                        , light.light_type == World::MapLightType::Spot
                                          ? spot_effective_cone_length(light)
                                          : 0.f
                                        });
      if (distance_sq(camera_pos, light.position) > (_cull_distance + viz_radius) * (_cull_distance + viz_radius))
      {
        continue;
      }

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

      cone_lines.clear();
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

      if (sel && *sel == li)
      {
        float const atten_end = std::max(0.5f, light.attenuation_end);
        glm::vec4 const teal { 0.2f, 0.75f, 0.75f, 0.35f };
        glm::vec4 const white { 1.f, 1.f, 1.f, 1.f };

        {
          OpenGL::Scoped::depth_mask_setter<GL_FALSE> const no_depth_write;
          OpenGL::Scoped::bool_setter<GL_BLEND, GL_TRUE> const enable_blend;
          gl.blendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

          _sphere_render.draw(
              mvp,
              light.position,
              teal,
              atten_end,
              32,
              18,
              teal.a,
              false);

          _sphere_render.draw(
              mvp,
              light.position,
              white,
              0.15f,
              16,
              12,
              1.f,
              false);
        }

        glm::vec3 const half { atten_end, atten_end, atten_end };
        glm::vec3 const min_extent = light.position - half;
        glm::vec3 const max_extent = light.position + half;
        _wirebox_render.draw(
            model_view,
            projection,
            glm::mat4x4{ 1.f },
            white,
            min_extent,
            max_extent);
      }
    }
  }

  drawSoundEmitterBillboards(model_view, projection, camera_pos, frustum, render_settings);
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

void WorldRender::setupTextureLayerBillboardResources()
{
  ZoneScopedN("WorldRender::setupTextureLayerBillboardResources");

  _tex_layer_billboard_ready = false;

  _tex_layer_billboard_program.reset(
    new OpenGL::program{
      { GL_VERTEX_SHADER, OpenGL::shader::src_from_qrc("texture_layer_billboard_vs") },
      { GL_FRAGMENT_SHADER, OpenGL::shader::src_from_qrc("texture_layer_billboard_fs") },
    });

  static constexpr glm::vec2 quad_strip[4] = {
    { -1.f, -1.f }, { 1.f, -1.f }, { -1.f, 1.f }, { 1.f, 1.f },
  };

  gl.bufferData<GL_ARRAY_BUFFER>(_tex_layer_billboard_quad_vbo, sizeof(quad_strip), quad_strip, GL_STATIC_DRAW);

  QImage const atlas = build_texture_layer_digit_atlas_image();
  if (atlas.isNull() || atlas.width() <= 0 || atlas.height() <= 0)
  {
    LogError << "WorldRender::setupTextureLayerBillboardResources: failed to build digit atlas" << std::endl;
    _tex_layer_billboard_program.reset();
    return;
  }

  if (_tex_layer_billboard_atlas == 0)
  {
    gl.genTextures(1, &_tex_layer_billboard_atlas);
  }

  gl.bindTexture(GL_TEXTURE_2D, _tex_layer_billboard_atlas);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl.texImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas.width(), atlas.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 atlas.constBits());
  gl.bindTexture(GL_TEXTURE_2D, 0);

  {
    OpenGL::Scoped::vao_binder const vao_bind(_tex_layer_billboard_vao);
    OpenGL::Scoped::use_program prog{ *_tex_layer_billboard_program.get() };

    prog.attrib("quad_corner", _tex_layer_billboard_quad_vbo, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    prog.attrib_divisor("quad_corner", 0, 1);

    prog.attrib("instance_center_digit", _tex_layer_billboard_instance_vbo, 4, GL_FLOAT, GL_FALSE,
                sizeof(TexLayerBillboardInstance), nullptr);
    prog.attrib_divisor("instance_center_digit", 1, 1);

    prog.attrib("instance_color", _tex_layer_billboard_instance_vbo, 4, GL_FLOAT, GL_FALSE,
                sizeof(TexLayerBillboardInstance), reinterpret_cast<void*>(sizeof(glm::vec4)));
    prog.attrib_divisor("instance_color", 1, 1);
  }

  _tex_layer_billboard_ready = true;
}

void WorldRender::drawTextureLayerCountBillboards ( glm::mat4x4 const& model_view
                                                 , glm::mat4x4 const& projection
                                                 , glm::vec3 const& camera_pos
                                                 , math::frustum const& frustum
                                                 , WorldRenderParams const& render_settings
                                                 )
{
  if (!render_settings.draw_texture_layer_count_overlay
      || render_settings.minimap_render
      || !render_settings.draw_terrain
      || render_settings.display_mode != display_mode::in_3D)
  {
    return;
  }
  if (!_tex_layer_billboard_ready || !_tex_layer_billboard_program || !_tex_layer_billboard_atlas)
  {
    return;
  }
  if (!_mcnk_program)
  {
    return;
  }

  ZoneScopedN("WorldRender::drawTextureLayerCountBillboards");

  constexpr float kTileSize = 533.33333f;
  constexpr float kChunkSize = kTileSize / 16.f;

  std::vector<TexLayerBillboardInstance> instances;
  instances.reserve(8192u);

  for (MapTile* tile : _world->mapIndex.loaded_tiles())
  {
    if (!tile || !tile->texturesFinishedLoading())
    {
      continue;
    }

    for (unsigned z = 0; z < 16u; ++z)
    {
      for (unsigned x = 0; x < 16u; ++x)
      {
        MapChunk* chunk = tile->getChunk(x, z);
        if (!chunk)
        {
          continue;
        }

        float const cx = (chunk->vmin.x + chunk->vmax.x) * 0.5f;
        float const cz = (chunk->vmin.z + chunk->vmax.z) * 0.5f;

        float surface_y = chunk->vcenter.y;
        if (!chunk->sampleTerrainHeightAt(cx, cz, surface_y))
        {
          glm::vec3 nearest;
          if (chunk->GetVertex(cx, cz, &nearest))
          {
            surface_y = nearest.y;
          }
        }

        // Small lift above the sampled mesh height (billboard is camera-facing, not axis-aligned).
        float const lift = kChunkSize * 0.04f;
        glm::vec3 const center(cx, surface_y + lift, cz);

        if (!frustum.intersectsSphere(center, kChunkSize * 0.6f))
        {
          continue;
        }

        int const layers = static_cast<int>(chunk->texture_set->num());
        int const n = std::clamp(layers, 0, 4);

        instances.push_back({ glm::vec4(center, static_cast<float>(n)), tex_layer_digit_color(n) });

        if (instances.size() >= kMaxTexLayerBillboardInstances)
        {
          goto tex_layer_billboard_done;
        }
      }
    }
  }

tex_layer_billboard_done:

  if (instances.empty())
  {
    return;
  }

  gl.bufferData<GL_ARRAY_BUFFER>(_tex_layer_billboard_instance_vbo,
                                  static_cast<GLsizeiptr>(instances.size() * sizeof(TexLayerBillboardInstance)),
                                  instances.data(),
                                  GL_DYNAMIC_DRAW);

  gl.enable(GL_BLEND);
  gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  OpenGL::Scoped::bool_setter<GL_CULL_FACE, GL_FALSE> const cull_off;
  OpenGL::Scoped::depth_mask_setter<GL_FALSE> const no_depth_write;

  float const d0 = glm::distance(camera_pos, glm::vec3(instances.front().center_digit));
  float const s = glm::clamp(0.22f * d0 / 140.f, 0.12f, 1.65f) * kChunkSize;
  constexpr float kTexLayerBillboardSize = 1.f / 8.f;
  // half_ext.x = world half-width, .y = world half-height (along camera-facing quad "up").
  constexpr float kTexLayerBillboardHeightScale = 0.5f;
  glm::vec2 const half_ext(s * 0.52f * kTexLayerBillboardSize,
                             s * 0.98f * kTexLayerBillboardSize * kTexLayerBillboardHeightScale);

  constexpr GLint kAtlasUnit = 14;
  gl.activeTexture(GL_TEXTURE0 + kAtlasUnit);
  gl.bindTexture(GL_TEXTURE_2D, _tex_layer_billboard_atlas);

  OpenGL::Scoped::use_program prog{ *_tex_layer_billboard_program.get() };
  prog.uniform("model_view", model_view);
  prog.uniform("projection", projection);
  prog.uniform("camera_pos", camera_pos);
  prog.uniform("billboard_half_extent", half_ext);
  prog.uniform("digit_atlas", kAtlasUnit);

  OpenGL::Scoped::vao_binder const vao_bind(_tex_layer_billboard_vao);
  gl.drawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(instances.size()));

  gl.bindTexture(GL_TEXTURE_2D, 0);
}

void WorldRender::setupSoundEmitterBillboardResources()
{
  ZoneScopedN("WorldRender::setupSoundEmitterBillboardResources");

  _sound_emitter_billboard_ready = false;

  _sound_emitter_billboard_program.reset(
    new OpenGL::program{
      { GL_VERTEX_SHADER, OpenGL::shader::src_from_qrc("sound_emitter_billboard_vs") },
      { GL_FRAGMENT_SHADER, OpenGL::shader::src_from_qrc("sound_emitter_billboard_fs") },
    });

  QImage icon(QStringLiteral(":/icons/sound_emitter.png"));
  if (icon.isNull())
  {
    LogError << "WorldRender::setupSoundEmitterBillboardResources: failed to load sound emitter icon" << std::endl;
    _sound_emitter_billboard_program.reset();
    return;
  }

  icon = icon.convertToFormat(QImage::Format_RGBA8888);

  if (_sound_emitter_icon_texture == 0)
  {
    gl.genTextures(1, &_sound_emitter_icon_texture);
  }

  gl.bindTexture(GL_TEXTURE_2D, _sound_emitter_icon_texture);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl.texImage2D(GL_TEXTURE_2D, 0, GL_RGBA, icon.width(), icon.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 icon.constBits());
  gl.bindTexture(GL_TEXTURE_2D, 0);

  {
    OpenGL::Scoped::vao_binder const vao_bind(_sound_emitter_billboard_vao);
    OpenGL::Scoped::use_program prog{ *_sound_emitter_billboard_program.get() };

    prog.attrib("quad_corner", _tex_layer_billboard_quad_vbo, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    prog.attrib_divisor("quad_corner", 0, 1);

    prog.attrib("instance_center", _sound_emitter_instance_vbo, 4, GL_FLOAT, GL_FALSE, sizeof(glm::vec4), nullptr);
    prog.attrib_divisor("instance_center", 1, 1);
  }

  _sound_emitter_billboard_ready = true;
}

void WorldRender::drawSoundEmitterBillboards ( glm::mat4x4 const& model_view
                                             , glm::mat4x4 const& projection
                                             , glm::vec3 const& camera_pos
                                             , math::frustum const& frustum
                                             , WorldRenderParams const& render_settings
                                             )
{
  if (!render_settings.draw_sound_emitters
      || render_settings.minimap_render
      || render_settings.display_mode != display_mode::in_3D)
  {
    return;
  }
  if (!_sound_emitter_billboard_ready || !_sound_emitter_billboard_program || !_sound_emitter_icon_texture)
  {
    return;
  }

  ZoneScopedN("WorldRender::drawSoundEmitterBillboards");

  constexpr float kCullRadius = 24.f;
  static constexpr std::size_t kMaxInstances = 8192u;

  std::vector<glm::vec4> instances;
  instances.reserve(512u);

  for (MapTile* tile : _world->mapIndex.loaded_tiles())
  {
    if (!tile || tile->loading_failed())
    {
      continue;
    }

    for (unsigned z = 0; z < 16u; ++z)
    {
      for (unsigned x = 0; x < 16u; ++x)
      {
        MapChunk* chunk = tile->getChunk(x, z);
        if (!chunk)
        {
          continue;
        }

        for (ENTRY_MCSE const& emitter : chunk->sound_emitters)
        {
          glm::vec3 const center(emitter.pos[0], emitter.pos[1], emitter.pos[2]);

          if (!frustum.intersectsSphere(center, kCullRadius))
          {
            continue;
          }

          float selection_weight = 1.f;
          if (render_settings.has_selected_sound_emitter)
          {
            glm::vec3 const sel_pos = render_settings.selected_sound_emitter_pos;
            if (glm::distance(center, sel_pos) < 0.05f)
              selection_weight = 2.f;
          }

          instances.emplace_back(center, selection_weight);

          if (instances.size() >= kMaxInstances)
          {
            goto sound_emitter_billboard_done;
          }
        }
      }
    }
  }

sound_emitter_billboard_done:

  if (instances.empty())
  {
    return;
  }

  gl.bufferData<GL_ARRAY_BUFFER>(_sound_emitter_instance_vbo,
                                  static_cast<GLsizeiptr>(instances.size() * sizeof(glm::vec4)),
                                  instances.data(),
                                  GL_DYNAMIC_DRAW);

  gl.enable(GL_BLEND);
  gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  OpenGL::Scoped::bool_setter<GL_CULL_FACE, GL_FALSE> const cull_off;
  OpenGL::Scoped::depth_mask_setter<GL_FALSE> const no_depth_write;
  OpenGL::Scoped::bool_setter<GL_DEPTH_TEST, GL_TRUE> const depth_test;

  float const d0 = glm::distance(camera_pos, glm::vec3(instances.front()));
  float const half_size = glm::clamp(0.35f * d0 / 140.f, 0.18f, 2.5f) * 3.f;
  glm::vec2 const half_ext(half_size, half_size);

  constexpr GLint kIconUnit = 13;
  gl.activeTexture(GL_TEXTURE0 + kIconUnit);
  gl.bindTexture(GL_TEXTURE_2D, _sound_emitter_icon_texture);

  OpenGL::Scoped::use_program prog{ *_sound_emitter_billboard_program.get() };
  prog.uniform("model_view", model_view);
  prog.uniform("projection", projection);
  prog.uniform("camera_pos", camera_pos);
  prog.uniform("billboard_half_extent", half_ext);
  prog.uniform("icon_texture", kIconUnit);

  OpenGL::Scoped::vao_binder const vao_bind(_sound_emitter_billboard_vao);
  gl.drawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(instances.size()));

  gl.bindTexture(GL_TEXTURE_2D, 0);
}

void WorldRender::drawBrushCursorOverlay ( glm::mat4x4 const& mvp
                                         , glm::vec3 const& cursor_pos
                                         , glm::vec4 const& cursor_color
                                         , WorldRenderParams const& render_settings
                                         )
{
  if (render_settings.cursor_type != CursorType::CIRCLE)
  {
    return;
  }
  if (render_settings.brush_radius <= 0.f)
  {
    return;
  }
  if (render_settings.minimap_render)
  {
    return;
  }

  Noggit::CursorRender::Mode mode;
  switch (render_settings.brush_cursor_style)
  {
    case BrushCursorStyle::FlatCircle:
      mode = Noggit::CursorRender::Mode::circle;
      break;
    case BrushCursorStyle::Sphere:
      mode = Noggit::CursorRender::Mode::sphere;
      break;
    default:
      return;
  }

  OpenGL::Scoped::bool_setter<GL_LINE_SMOOTH, GL_TRUE> const line_smooth;
  gl.hint(GL_LINE_SMOOTH_HINT, GL_NICEST);

  _cursor_render.draw(mode
                    , mvp
                    , cursor_color
                    , cursor_pos
                    , render_settings.brush_radius
                    , render_settings.inner_radius_ratio
                    );
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

    GLint viewport[4]{};
    gl.getIntegerv(GL_VIEWPORT, viewport);
    float const aspect = static_cast<float>(std::max(viewport[2], 1))
                       / static_cast<float>(std::max(viewport[3], 1));
    // Stable on-screen disk size (shader offsets in NDC after perspective divide).
    float const point_size_ndc =
      glm::clamp(0.010f + 0.010f * (point_radius / 1.6f), 0.006f, 0.028f);
    viz.uniform("point_size_ndc", point_size_ndc);
    viz.uniform("aspect", aspect);

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

void WorldRender::drawSeaLevelPlane ( glm::mat4x4 const& model_view
                                    , glm::mat4x4 const& projection
                                    , glm::vec3 const& camera_pos
                                    , float plane_radius
                                    , glm::vec4 const& sea_color
                                    )
{
  if (!_sea_level_clip_program.get() || !_sea_level_clip_vao || !_sea_level_clip_vbo)
    return;

  OpenGL::Scoped::bool_setter<GL_CULL_FACE, GL_FALSE> const sea_two_sided;

  // Far-terrain flicker: one pair of huge float triangles spans tens of km; GPU loses precision on
  // clip/depth vs terrain. Build many smaller quads; compute corners in double then cast to float.
  static constexpr int kSeaPlaneGridSteps = 40;
  static constexpr double kSeaLevelWorldY = 0.0;

  double const cx = static_cast<double>(camera_pos.x);
  double const cz = static_cast<double>(camera_pos.z);
  double const rd = static_cast<double>(plane_radius);
  double const cell = (2.0 * rd) / static_cast<double>(kSeaPlaneGridSteps);

  std::vector<glm::vec3>& verts = _sea_level_plane_mesh_scratch;
  verts.clear();
  verts.reserve(static_cast<std::size_t>(kSeaPlaneGridSteps * kSeaPlaneGridSteps * 6u));

  for (int j = 0; j < kSeaPlaneGridSteps; ++j)
  {
    for (int i = 0; i < kSeaPlaneGridSteps; ++i)
    {
      double const x0 = cx - rd + static_cast<double>(i) * cell;
      double const x1 = x0 + cell;
      double const z0 = cz - rd + static_cast<double>(j) * cell;
      double const z1 = z0 + cell;

      auto const push = [&verts] (double x, double z, double y)
      {
        verts.emplace_back(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
      };

      push(x0, z0, kSeaLevelWorldY);
      push(x1, z0, kSeaLevelWorldY);
      push(x1, z1, kSeaLevelWorldY);
      push(x0, z0, kSeaLevelWorldY);
      push(x1, z1, kSeaLevelWorldY);
      push(x0, z1, kSeaLevelWorldY);
    }
  }

  OpenGL::Scoped::use_program shader(*_sea_level_clip_program.get());
  shader.uniform("model_view", model_view);
  shader.uniform("projection", projection);
  shader.uniform("color", sea_color);

  gl.bindBuffer(GL_ARRAY_BUFFER, _sea_level_clip_vbo);
  gl.bufferData(GL_ARRAY_BUFFER
                 , static_cast<GLsizeiptr>(verts.size() * sizeof(glm::vec3))
                 , verts.data()
                 , GL_DYNAMIC_DRAW);

  OpenGL::Scoped::vao_binder const bind(_sea_level_clip_vao);
  gl.drawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
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

  _sea_level_clip_program.reset();
  if (_sea_level_clip_vao)
  {
    gl.deleteVertexArray(1, &_sea_level_clip_vao);
    _sea_level_clip_vao = 0;
  }
  if (_sea_level_clip_vbo)
  {
    gl.deleteBuffers(1, &_sea_level_clip_vbo);
    _sea_level_clip_vbo = 0;
  }
  _occluder_program.reset();
  _sun_shadow_m2_program.reset();
  _sun_shadow_wmo_program.reset();
  _gpu_sun_shadow.unload();

  _mccv_viz_program.reset();
  _mccv_crosshair_ndc_program.reset();
  _mccv_viz_buffers_ready = false;

  _tex_layer_billboard_program.reset();
  if (_tex_layer_billboard_atlas)
  {
    gl.deleteTextures(1, &_tex_layer_billboard_atlas);
    _tex_layer_billboard_atlas = 0;
  }
  _tex_layer_billboard_ready = false;

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

  gl.bindBuffer(GL_UNIFORM_BUFFER, _mvp_ubo);
  gl.bufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(OpenGL::MVPUniformBlock), &_mvp_ubo_data);
}

void WorldRender::updateModernFogUniformBlock(bool draw_fog, bool draw_volumetric_fog
                                            , glm::vec3 const& camera_pos, bool camera_moved)
{
  ZoneScoped;

  bool const modern_features = noggit_modern_features_enabled();
  bool const want_atmos = modern_features && draw_fog && _skies;
  bool const want_vfog = modern_features && draw_volumetric_fog;
  if ((!want_atmos && !want_vfog) || !_skies)
  {
    _modern_fog_ubo_data = {};
    gl.bindBuffer(GL_UNIFORM_BUFFER, _modern_fog_ubo);
    gl.bufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(OpenGL::ModernFogUniformBlock), &_modern_fog_ubo_data);
    return;
  }

  constexpr float k_vfog_rebuild_dist_sq = 96.f * 96.f;
  glm::vec3 const fog_delta = camera_pos - _last_modern_fog_camera_pos;
  float const fog_move_sq = fog_delta.x * fog_delta.x + fog_delta.y * fog_delta.y + fog_delta.z * fog_delta.z;
  bool const rebuild_vfog = !camera_moved || fog_move_sq >= k_vfog_rebuild_dist_sq
    || (_last_modern_fog_camera_pos.x == std::numeric_limits<float>::max())
    || _last_draw_volumetric_fog != draw_volumetric_fog;
  _last_draw_volumetric_fog = draw_volumetric_fog;

  // meta.x: modern atmospheric fog coeffs active; meta.y: VFOG count (filled below).
  _modern_fog_ubo_data.meta.x = want_atmos ? 1 : 0;
  if (want_atmos)
  {
    // FogDensity is retail height/atmospheric density — not the classic distance-fog
    // power exponent. Leave .x at 0 so shaders keep using LightDir_FogRate.w (fogRate).
    _modern_fog_ubo_data.fog_density_end_height.x = 0.f;
    _modern_fog_ubo_data.fog_density_end_height.y = _skies->end_fog_color_distance();
    _modern_fog_ubo_data.fog_density_end_height.z = _skies->fog_height();
    _modern_fog_ubo_data.fog_density_end_height.w = _skies->fog_height_scaler();

    glm::vec3 const end_col = _skies->end_fog_color();
    _modern_fog_ubo_data.end_fog_color = { end_col.x, end_col.y, end_col.z, 0.f };

    glm::vec3 const fh_col = _skies->fog_height_color();
    _modern_fog_ubo_data.fog_height_color_density = {
      fh_col.x, fh_col.y, fh_col.z, _skies->fog_height_density()
    };

    auto const hc = _skies->fog_height_coeff();
    _modern_fog_ubo_data.height_coeff_01 = { hc[0], hc[1], hc[2], hc[3] };
    auto const mc = _skies->main_fog_coeff();
    _modern_fog_ubo_data.height_coeff_23 = { mc[0], mc[1], mc[2], mc[3] };
  }
  else
  {
    _modern_fog_ubo_data.fog_density_end_height = {};
    _modern_fog_ubo_data.end_fog_color = {};
    _modern_fog_ubo_data.fog_height_color_density = {};
    _modern_fog_ubo_data.height_coeff_01 = {};
    _modern_fog_ubo_data.height_coeff_23 = {};
  }

  if (rebuild_vfog)
  {
    _last_modern_fog_camera_pos = camera_pos;

    for (int i = 0; i < OpenGL::kMaxGpuVolumetricFogs; ++i)
    {
      _modern_fog_ubo_data.vfog_pos_radius[i] = {};
      _modern_fog_ubo_data.vfog_color_intensity[i] = {};
      _modern_fog_ubo_data.vfog_radius_xyz[i] = {};
    }

    int vfog_count = 0;
    if (want_vfog)
    {
      auto const& vfogs = _world->volumetricFogs();
      std::vector<std::pair<float, std::size_t>> vfog_order;
      vfog_order.reserve(vfogs.size());
      for (std::size_t i = 0; i < vfogs.size(); ++i)
      {
        // Skip ultra-high fog levels that the client often hides in the editor view.
        if (vfogs[i].fog_level > 2u)
          continue;
        float const d = glm::distance(camera_pos, vfogs[i].position);
        vfog_order.emplace_back(d, i);
      }
      std::sort(vfog_order.begin(), vfog_order.end());

      // Prefer a generous cull so nearby volumes aren't dropped when distance fog is off
      // (cull_distance then equals view distance, but volumes can still be just outside).
      float const vfog_cull = std::max(_cull_distance, _view_distance);

      for (auto const& [dist, idx] : vfog_order)
      {
        (void)dist;
        if (vfog_count >= OpenGL::kMaxGpuVolumetricFogs)
          break;
        auto const& v = vfogs[idx];
        float const max_r = std::max({ v.radius[0], v.radius[1], v.radius[2], 1.f });
        if (glm::distance(camera_pos, v.position) > max_r * 4.f + vfog_cull)
          continue;

        float const intensity = volumetric_fog_shader_intensity(
          v.intensity[0], v.intensity[1], v.intensity[2]);
        _modern_fog_ubo_data.vfog_pos_radius[vfog_count] = { v.position.x, v.position.y, v.position.z, max_r };
        _modern_fog_ubo_data.vfog_color_intensity[vfog_count] = {
          v.color.x, v.color.y, v.color.z, intensity
        };
        // On-disk radius[2] is a thin vertical slab (~0.3–22). Keep XZ accurate but
        // give a small minimum thickness so ground fog is visible on terrain.
        constexpr float k_min_xz = 1.f;
        constexpr float k_min_y = 12.f;
        _modern_fog_ubo_data.vfog_radius_xyz[vfog_count] = {
          std::max(v.radius[0], k_min_xz)
        , std::max(v.radius[1], k_min_y)
        , std::max(v.radius[2], k_min_xz)
        , 0.f
        };
        ++vfog_count;
      }

      if (vfogs.empty() && want_vfog)
      {
        // One-shot hint when the toggle is on but nothing loaded for this map.
        static bool logged_empty = false;
        if (!logged_empty)
        {
          LogDebug << "Volumetric fog toggle is on but no VFOG entries are loaded for this map."
                   << std::endl;
          logged_empty = true;
        }
      }
    }
    _modern_fog_ubo_data.meta.y = vfog_count;
  }

  gl.bindBuffer(GL_UNIFORM_BUFFER, _modern_fog_ubo);
  gl.bufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(OpenGL::ModernFogUniformBlock), &_modern_fog_ubo_data);
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

void WorldRender::updatePointLightsUniformBlock(bool enabled, glm::vec3 const& camera_pos, bool camera_moved)
{
  ZoneScoped;

  (void) camera_moved;

  _point_lights_ubo_data.meta = { 0, enabled ? 1 : 0, 0, 0 };

  if (enabled)
  {
    int count = 0;
    float const flicker_t = _world->animtime * 0.001f;
    std::size_t const n = _world->_point_lights.size();

    auto upload_one = [&] (std::size_t li)
    {
      if (count >= OpenGL::kMaxGpuPointLights)
      {
        return;
      }

      auto const& light = _world->_point_lights[li];

      float radius = std::max(0.0f, light.attenuation_end);
      float atten_end = light.attenuation_end;
      if (light.light_type == World::MapLightType::Spot)
      {
        float const cone_len = spot_effective_cone_length(light);
        radius = std::max(radius, cone_len);
        atten_end = std::max(atten_end, cone_len);
      }

      float const flicker_mul = point_light_intensity_multiplier(light, flicker_t);
      float const eff_intensity = light.intensity * flicker_mul;

      _point_lights_ubo_data.position_radius[count] = { light.position, radius };
      _point_lights_ubo_data.color_intensity[count] = { light.color, eff_intensity };
      _point_lights_ubo_data.attenuation[count] = { light.attenuation_start, atten_end, 0.f, 0.f };

      if (light.light_type == World::MapLightType::Spot)
      {
        glm::vec3 const fwd = map_light_forward(light);
        float const ci = std::cos(light.inner_angle);
        float const co = std::cos(light.outer_angle);
        _point_lights_ubo_data.spot_dir_cos_inner[count] = { fwd, ci };
        _point_lights_ubo_data.spot_cos_outer_kind[count] = { co, 1.f, 0.f, 0.f };
      }
      else
      {
        _point_lights_ubo_data.spot_dir_cos_inner[count] = { 0.f, 0.f, 1.f, 1.f };
        _point_lights_ubo_data.spot_cos_outer_kind[count] = { -1.f, 0.f, 0.f, 0.f };
      }

      ++count;
    };

    if (n <= static_cast<std::size_t>(OpenGL::kMaxGpuPointLights))
    {
      for (std::size_t li = 0; li < n; ++li)
      {
        upload_one(li);
      }
    }
    else
    {
      constexpr float k_resort_dist_sq = 128.f * 128.f;
      bool const need_resort = _point_light_sort_light_count != n
                            || _point_light_sort_order.size() != n
                            || distance_sq(camera_pos, _point_light_sort_camera_pos) > k_resort_dist_sq;

      if (need_resort)
      {
        _point_light_sort_order.resize(n);
        for (std::size_t i = 0; i < n; ++i)
        {
          _point_light_sort_order[i] = i;
        }

        std::sort(_point_light_sort_order.begin(), _point_light_sort_order.end(),
                  [&] (std::size_t a, std::size_t b)
                  {
                    return distance_sq(_world->_point_lights[a].position, camera_pos)
                         < distance_sq(_world->_point_lights[b].position, camera_pos);
                  });

        _point_light_sort_camera_pos = camera_pos;
        _point_light_sort_light_count = n;
      }

      for (std::size_t const li : _point_light_sort_order)
      {
        upload_one(li);
      }
    }

    _point_lights_ubo_data.meta.x = count;
  }

  gl.bindBuffer(GL_UNIFORM_BUFFER, _point_lights_ubo);
  gl.bufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(OpenGL::PointLightsUniformBlock), &_point_lights_ubo_data);
}

void WorldRender::drawVolumetricFogDebug(glm::mat4x4 const& model_view
                                        , glm::mat4x4 const& projection
                                        , glm::vec3 const& camera_pos
                                        , float cull_distance)
{
  ZoneScoped;

  for (auto const& vfog : _world->volumetricFogs())
  {
    float const max_r = std::max({ vfog.radius[0], vfog.radius[1], vfog.radius[2] });
    if (glm::distance(camera_pos, vfog.position) > cull_distance + max_r)
      continue;

    glm::vec4 const col(vfog.color, 0.35f);
    _sphere_render.draw(model_view * projection, vfog.position, col, max_r, 24, 16, 1.f
                        , false, false, true);
  }
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
  renderParams.draw_volumetric_fog = false;
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

void WorldRender::collectVisibleObjects ( int frame
                                        , glm::mat4x4 const& /*model_view*/
                                        , glm::vec3 const& camera_pos
                                        , math::frustum const& frustum
                                        , WorldRenderParams const& render_settings
                                        , MinimapRenderSettings* minimap_render_settings
                                        , tsl::robin_map<Model*, std::vector<glm::mat4x4>>& models_to_draw
                                        , std::vector<WMOInstance*>& wmos_to_draw
                                        )
{
  ZoneScopedN ("World::draw() : Collect visible objects");

  for (auto const& tile_pair : _world->_loaded_tiles_buffer)
  {
    MapTile* tile = tile_pair.second;

    if (!tile)
    {
      break;
    }

    if (render_settings.minimap_render)
    {
      tile->renderer()->setOccluded (false);
    }

    if (tile->renderer()->isOccluded() && !tile->getChunkUpdateFlags() && !tile->renderer()->isOverridingOcclusionCulling())
    {
      continue;
    }

    if (tile->camDist() > _cull_distance)
    {
      continue;
    }

    for (auto& object_pair : tile->getObjectInstances())
    {
      if (!object_pair.first->finishedLoading())
      {
        continue;
      }

      if (object_pair.second[0]->which() == eMODEL)
      {
        if (!render_settings.draw_models && !(render_settings.minimap_render && minimap_render_settings->use_filters))
        {
          for (auto& instance : object_pair.second)
          {
            instance->_rendered_last_frame = false;
          }
          continue;
        }

        auto& instances = models_to_draw[reinterpret_cast<Model*> (object_pair.first)];

        if (tile->renderer()->objectsFrustumCullTest() > 1)
        {
          instances.reserve (instances.size() + object_pair.second.size());
        }
        else
        {
          instances.reserve (instances.size() + object_pair.second.size() / 2);
        }

        for (auto& instance : object_pair.second)
        {
          instance->_rendered_last_frame = false;

          if (instance->frame == frame)
          {
            instance->_rendered_last_frame = true;
            continue;
          }

          auto m2_instance = static_cast<ModelInstance*> (instance);

          if (!render_settings.draw_hidden_models && m2_instance->model->is_hidden())
          {
            continue;
          }

          instance->frame = frame;

          bool render = false;
          if (!render_settings.camera_moved && !m2_instance->extentsDirty())
          {
            if (m2_instance->_rendered_last_frame)
            {
              render = true;
            }
          }
          if (!render && m2_instance->isInRenderDist (_cull_distance, camera_pos, render_settings.display_mode)
              && (tile->renderer()->objectsFrustumCullTest() > 1 || m2_instance->isInFrustum (frustum)))
          {
            render = true;
          }

          if (!render)
          {
            continue;
          }

          instances.emplace_back (m2_instance->transformMatrix());
          m2_instance->_rendered_last_frame = true;
        }
      }
      else if (object_pair.second[0]->which() == eWMO)
      {
        if (!render_settings.draw_wmo)
        {
          for (auto& instance : object_pair.second)
          {
            instance->_rendered_last_frame = false;
          }
          continue;
        }

        if (tile->renderer()->objectsFrustumCullTest() > 1)
        {
          wmos_to_draw.reserve (wmos_to_draw.size() + object_pair.second.size());
        }
        else
        {
          wmos_to_draw.reserve (wmos_to_draw.size() + object_pair.second.size() / 2);
        }

        for (auto& instance : object_pair.second)
        {
          instance->_rendered_last_frame = false;

          if (instance->frame == frame)
          {
            instance->_rendered_last_frame = true;
            continue;
          }

          auto wmo_instance = static_cast<WMOInstance*> (instance);

          if (!render_settings.draw_hidden_models && wmo_instance->wmo->is_hidden())
          {
            continue;
          }

          instance->frame = frame;

          bool render = false;
          if (!render_settings.camera_moved && !wmo_instance->extentsDirty())
          {
            if (wmo_instance->_rendered_last_frame)
            {
              render = true;
            }
          }
          if ((!render && tile->renderer()->objectsFrustumCullTest() > 1)
              || frustum.intersects (wmo_instance->getExtents()[1], wmo_instance->getExtents()[0]))
          {
            render = true;
          }

          if (render)
          {
            wmos_to_draw.emplace_back (wmo_instance);
            wmo_instance->_rendered_last_frame = true;

            if (render_settings.draw_wmo_doodads)
            {
              std::map<std::uint32_t, std::vector<wmo_doodad_instance>>* doodads
                = wmo_instance->get_doodads (render_settings.draw_hidden_models);

              if (!doodads)
              {
                continue;
              }

              for (auto& doodad_pair : *doodads)
              {
                for (auto& doodad : doodad_pair.second)
                {
                  if (doodad.frame == frame)
                  {
                    continue;
                  }
                  doodad.frame = frame;

                  if (doodad.model->use_fake_geometry())
                  {
                    continue;
                  }

                  if (!doodad.isInRenderDist (_cull_distance, camera_pos, render_settings.display_mode))
                  {
                    continue;
                  }

                  auto& doodad_instances = models_to_draw[doodad.model.get()];
                  doodad_instances.emplace_back (doodad.transformMatrix());
                }
              }
            }
          }
        }
      }
    }
  }
}
