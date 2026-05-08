// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#ifndef NOGGIT_WORLDRENDER_HPP
#define NOGGIT_WORLDRENDER_HPP

#include <noggit/rendering/BaseRender.hpp>

#include <external/glm/glm.hpp>

#include <noggit/tool_enums.hpp>
#include <noggit/rendering/CursorRender.hpp>
#include <noggit/rendering/LiquidTextureManager.hpp>
#include <noggit/map_horizon.h>
#include <noggit/Sky.h>

#include <noggit/rendering/Primitives.hpp>

#include <QOpenGLFramebufferObject>

#include <memory>

namespace OpenGL
{
  struct program;
}

struct TileIndex;
class World;
struct MinimapRenderSettings;


struct WorldRenderParams 
{
  float cursorRotation;
  CursorType cursor_type;
  float brush_radius;
  bool show_unpaintable_chunks;
  bool draw_only_inside_light_sphere;
  bool draw_wireframe_light_sphere;
  float alpha_light_sphere;
  bool draw_point_lights;
  bool draw_point_light_spheres;
  float point_light_sphere_opacity;
  float inner_radius_ratio;
  float angle;
  float orientation;
  bool use_ref_pos;
  bool angled_mode;
  bool draw_paintability_overlay;
  editing_mode editing_mode;
  bool camera_moved;
  bool draw_mfbo;
  bool draw_terrain;
  bool draw_wmo;
  bool draw_water;
  bool draw_wmo_doodads;
  bool draw_models;
  bool draw_model_animations;
  bool draw_models_with_box;
  bool draw_hidden_models;
  bool draw_sky;
  bool draw_skybox;
  bool draw_fog;
  eTerrainType ground_editing_brush;
  int water_layer;
  display_mode display_mode;
  bool draw_occlusion_boxes;
  bool minimap_render;
  bool draw_wmo_exterior;

  bool render_select_m2_aabb;
  bool render_select_m2_collission_bbox;
  bool render_select_wmo_aabb;
  bool render_select_wmo_groups_bounds;

  /// Vertex-paint (MCCV) preview: hold Alt in mccv mode. Uses brush radius / cursor color from tool.
  bool draw_mccv_vertex_alt_viz = false;
  glm::vec3 mccv_viz_hub{};
  float mccv_viz_radius = 0.f;
  bool mccv_viz_hub_valid = false;
  glm::vec2 mccv_viz_hub_ndc{};

  /// Ramp creation tool preview (Editor menu)
  bool draw_ramp_preview = false;
  glm::vec3 ramp_preview_a{};
  glm::vec3 ramp_preview_b{};
  float ramp_preview_radius = 0.f;
  float ramp_preview_cap_len = 0.f;

  /// Highlight quick-selected ADT chunks (Chunk Manipulator tool).
  bool draw_chunk_manipulator_selection = false;
};

namespace Noggit::Rendering
{
  class WorldRender : public BaseRender
  {
  public:
    WorldRender(World* world);

    void upload() override;
    void unload() override;

    void draw (glm::mat4x4 const& model_view
        , glm::mat4x4 const& projection
        , glm::vec3 const& cursor_pos
        , glm::vec4 const& cursor_color
        , glm::vec3 const& ref_pos
        , glm::vec3 const& camera_pos
        , MinimapRenderSettings* minimap_render_settings
        , WorldRenderParams const& render_settings
    );

    bool saveMinimap (TileIndex const& tile_idx
                      , MinimapRenderSettings* settings
                      , std::optional<QImage>& combined_image);

    [[nodiscard]]
    OpenGL::TerrainParamsUniformBlock* getTerrainParamsUniformBlock();;

    void updateTerrainParamsUniformBlock();
    void markTerrainParamsUniformBlockDirty();;

    [[nodiscard]] std::unique_ptr<Skies>& skies();;

    float _view_distance;
    float cullDistance() const;

    unsigned int _frame_max_chunk_updates = 256;

    bool directional_lightning;
    bool local_lightning;

  private:
    void drawMinimap ( MapTile *tile
        , glm::mat4x4 const& model_view
        , glm::mat4x4 const& projection
        , glm::vec3 const& camera_pos
        , MinimapRenderSettings* settings
    );

    void updateMVPUniformBlock(const glm::mat4x4& model_view, const glm::mat4x4& projection);
    void updateLightingUniformBlock(bool draw_fog, glm::vec3 const& camera_pos);
    void updateLightingUniformBlockMinimap(MinimapRenderSettings* settings);

    void setupChunkVAO(OpenGL::Scoped::use_program& mcnk_shader);
    void setupLiquidChunkVAO(OpenGL::Scoped::use_program& water_shader);
    void setupOccluderBuffers();
    void setupChunkBuffers();
    void setupLiquidChunkBuffers();

    World* _world;
    float _cull_distance;

    // shaders
    std::unique_ptr<OpenGL::program> _mcnk_program;;
    std::unique_ptr<OpenGL::program> _mfbo_program;
    std::unique_ptr<OpenGL::program> _m2_program;
    std::unique_ptr<OpenGL::program> _m2_instanced_program;
    std::unique_ptr<OpenGL::program> _m2_particles_program;
    std::unique_ptr<OpenGL::program> _m2_ribbons_program;
    std::unique_ptr<OpenGL::program> _m2_box_program;
    std::unique_ptr<OpenGL::program> _wmo_program;
    std::unique_ptr<OpenGL::program> _liquid_program;
    std::unique_ptr<OpenGL::program> _occluder_program;

    // horizon && skies && lighting
    std::unique_ptr<Noggit::map_horizon::render> _horizon_render;
    std::unique_ptr<OutdoorLighting> _outdoor_lighting;
    OutdoorLightStats _outdoor_light_stats;
    std::unique_ptr<Skies> _skies;

    // cursor
    Noggit::CursorRender _cursor_render;
    Noggit::Rendering::Primitives::Sphere _sphere_render;
    Noggit::Rendering::Primitives::Square _square_render;
    Noggit::Rendering::Primitives::Line _line_render;
    Noggit::Rendering::Primitives::WireBox _wirebox_render;

    // buffers
    OpenGL::Scoped::deferred_upload_buffers<12> _buffers;
    GLuint const& _mvp_ubo = _buffers[0];
    GLuint const& _lighting_ubo = _buffers[1];
    GLuint const& _terrain_params_ubo = _buffers[2];
    GLuint const& _mapchunk_vertex = _buffers[3];
    GLuint const& _mapchunk_index = _buffers[4];
    GLuint const& _mapchunk_texcoord = _buffers[5];
    GLuint const& _liquid_chunk_vertex = _buffers[6];
    GLuint const& _occluder_index = _buffers[7];
    GLuint const& _point_lights_ubo = _buffers[8];
    GLuint const& _mccv_viz_instance_vbo = _buffers[9];
    GLuint const& _mccv_viz_quad_vbo = _buffers[10];
    GLuint const& _mccv_viz_crosshair_vbo = _buffers[11];

    // uniform blocks
    OpenGL::MVPUniformBlock _mvp_ubo_data;
    OpenGL::LightingUniformBlock _lighting_ubo_data;
    OpenGL::TerrainParamsUniformBlock _terrain_params_ubo_data;
    OpenGL::PointLightsUniformBlock _point_lights_ubo_data;

    // VAOs
    OpenGL::Scoped::deferred_upload_vertex_arrays<5> _vertex_arrays;
    GLuint const& _mapchunk_vao = _vertex_arrays[0];
    GLuint const& _liquid_chunk_vao = _vertex_arrays[1];
    GLuint const& _occluder_vao = _vertex_arrays[2];
    GLuint const& _mccv_viz_vao = _vertex_arrays[3];
    GLuint const& _mccv_crosshair_vao = _vertex_arrays[4];

    LiquidTextureManager _liquid_texture_manager;

    bool _need_terrain_params_ubo_update = false;

    void updatePointLightsUniformBlock(bool enabled, glm::vec3 const& camera_pos);

    void setupMccvVizBuffers();
    void drawMccvVertexAltViz ( glm::mat4x4 const& model_view
                              , glm::mat4x4 const& projection
                              , glm::mat4x4 const& mvp
                              , glm::vec3 const& camera_pos
                              , glm::vec4 const& cursor_color
                              , WorldRenderParams const& render_settings
                              );

    void drawRampPreview(glm::mat4x4 const& mvp, WorldRenderParams const& render_settings);

    void drawChunkManipulatorSelection ( glm::mat4x4 const& model_view
                                       , glm::mat4x4 const& projection
                                       , WorldRenderParams const& render_settings
                                       );

    // Terrain→WMO seam blending: terrain base-color lookup (rendered top-down).
    std::unique_ptr<QOpenGLFramebufferObject> _terrain_blend_fbo;
    GLuint _terrain_blend_color_tex = 0;
    glm::vec2 _terrain_blend_origin_xz{};
    float _terrain_blend_inv_size = 0.f;
    float _terrain_blend_world_size = 533.33333f * 2.f; // 2 tiles around camera
    int _terrain_blend_tex_size = 1024;
    glm::vec2 _terrain_blend_last_center_xz{};

    std::unique_ptr<OpenGL::program> _mccv_viz_program;
    std::unique_ptr<OpenGL::program> _mccv_crosshair_ndc_program;
    bool _mccv_viz_buffers_ready = false;
  };
}

#endif //NOGGIT_WORLDRENDER_HPP
