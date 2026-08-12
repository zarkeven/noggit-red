// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <opengl/types.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <limits>

namespace Noggit::Rendering
{
  struct SunShadowMatrices
  {
    glm::mat4 view{1.f};
    glm::mat4 projection{1.f};
    glm::mat4 view_proj{1.f};
    glm::mat4 view_proj_bias{1.f};
  };

  SunShadowMatrices compute_sun_shadow_matrices (
    glm::vec3 const& center
  , glm::vec3 const& sun_dir
  , float ortho_half_extent
  , float far_plane
  , int shadow_map_size
  );

  class RealtimeGpuShadowMap
  {
  public:
    void invalidate();
    void unload();

    void prepare_frame (
      glm::vec3 const& center
    , glm::vec3 const& sun_dir
    , float ortho_half_extent
    , float far_plane
    );

    void begin_depth_pass();
    void end_depth_pass();

    [[nodiscard]] bool ready() const { return _valid; }
    [[nodiscard]] GLuint depth_texture() const { return _depth_tex; }
    [[nodiscard]] SunShadowMatrices const& matrices() const { return _matrices; }
    [[nodiscard]] glm::vec3 const& sun_direction() const { return _sun_dir; }
    [[nodiscard]] glm::vec3 light_travel_direction() const { return -_sun_dir; }

    class ScopedDepthPass
    {
    public:
      explicit ScopedDepthPass(RealtimeGpuShadowMap& map);
      ~ScopedDepthPass();
      ScopedDepthPass(ScopedDepthPass const&) = delete;
      ScopedDepthPass& operator=(ScopedDepthPass const&) = delete;

    private:
      RealtimeGpuShadowMap* _map;
    };

  private:
    void ensure_resources();

    GLuint _fbo = 0;
    GLuint _depth_tex = 0;
    GLuint _dummy_color_tex = 0;
    int _size = 4096;
    int _allocated_size = 0;
    bool _valid = false;
    bool _pass_active = false;
    glm::ivec2 _last_snap_texel { std::numeric_limits<int>::min(), std::numeric_limits<int>::min() };
    SunShadowMatrices _matrices{};
    glm::vec3 _sun_dir{0.f, 1.f, 0.f};
  };
}
