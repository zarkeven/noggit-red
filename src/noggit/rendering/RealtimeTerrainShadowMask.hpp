// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/tool_enums.hpp>

#include <opengl/types.hpp>

#include <external/tsl/robin_map.h>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <limits>
#include <vector>

class Model;
class World;
class WMOInstance;
struct WorldRenderParams;

namespace Noggit::Rendering
{
  class RealtimeTerrainShadowMask
  {
  public:
    void invalidate();

    bool update ( World* world
                , glm::vec3 const& camera_pos
                , glm::vec3 const& sun_dir
                , float max_shadow_distance
                , glm::mat4x4 const& model_view
                , WorldRenderParams const& render_settings
                , bool animate_models
                , tsl::robin_map<Model*, std::vector<glm::mat4x4>> const* culled_models
                , std::vector<WMOInstance*> const* culled_wmos
                );

    [[nodiscard]] GLuint texture() const { return _tex; }
    [[nodiscard]] glm::vec2 origin_xz() const { return _origin_xz; }
    [[nodiscard]] float inv_world_size() const { return _inv_world_size; }
    [[nodiscard]] float inv_texel_uv() const;
    [[nodiscard]] bool ready_for_sampling() const { return _tex != 0 && _sun_dir_initialized && _bake_cursor > 0; }

  private:
    struct OccluderAabb
    {
      float min_x;
      float max_x;
      float min_z;
      float max_z;
    };

    void flush_deferred_gl_delete();
    void ensure_texture();
    void begin_rebake (glm::ivec2 const& snap_origin, glm::vec3 const& sun_dir);
    void build_bake_order_spiral (glm::vec3 const& camera_pos);
    void rebuild_occluder_bounds (
      tsl::robin_map<Model*, std::vector<glm::mat4x4>> const* culled_models
    , std::vector<WMOInstance*> const* culled_wmos
    , WorldRenderParams const& render_settings
    );
    void upload_if_dirty();
    void note_dirty_texel (int ix, int iz);
    void reset_dirty_rect();
    [[nodiscard]] bool texel_near_occluder (float wx, float wz) const;
    [[nodiscard]] std::optional<float> cached_ground_y (
      World* world, float wx, float wz, float reference_y
    );
    bool bake_texel ( World* world
                    , int ix
                    , int iz
                    , glm::vec3 const& camera_pos
                    , glm::vec3 const& sun_dir
                    , float max_shadow_distance
                    , glm::mat4x4 const& model_view
                    , WorldRenderParams const& render_settings
                    , bool animate_models
                    , tsl::robin_map<Model*, std::vector<glm::mat4x4>> const* culled_models
                    , std::vector<WMOInstance*> const* culled_wmos
                    , std::vector<std::uint8_t>& buffer
                    );
    bool refresh_near_occluders ( World* world
                                , glm::vec3 const& camera_pos
                                , glm::vec3 const& sun_dir
                                , float max_shadow_distance
                                , glm::mat4x4 const& model_view
                                , WorldRenderParams const& render_settings
                                , bool animate_models
                                , tsl::robin_map<Model*, std::vector<glm::mat4x4>> const* culled_models
                                , std::vector<WMOInstance*> const* culled_wmos
                                , int ray_budget
                                );

    GLuint _tex = 0;
    int _allocated_size = 0;
    int _size = 512;
    float _world_size = 533.33333f * 2.f;
    float _texel_world = 0.f;
    glm::vec2 _origin_xz{};
    float _inv_world_size = 0.f;
    glm::ivec2 _snap_origin { std::numeric_limits<int>::min(), std::numeric_limits<int>::min() };

    glm::vec3 _last_sun_dir{0.f};
    bool _sun_dir_initialized = false;
    bool _bake_complete = false;
    bool _gpu_dirty = false;

    int _bake_cursor = 0;
    int _frame_counter = 0;
    std::vector<std::uint8_t> _pixels;
    std::vector<std::uint8_t> _scratch;
    std::vector<std::uint8_t> _visit_scratch;
    std::vector<int> _bake_order;
    std::vector<OccluderAabb> _occluder_bounds;
    tsl::robin_map<std::uint64_t, float> _ground_y_cache;

    int _dirty_min_ix = 0;
    int _dirty_max_ix = -1;
    int _dirty_min_iz = 0;
    int _dirty_max_iz = -1;

    bool _defer_gl_delete = false;
  };
}
