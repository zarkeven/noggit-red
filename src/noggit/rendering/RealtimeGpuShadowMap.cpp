// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/rendering/RealtimeGpuShadowMap.hpp>

#include <opengl/context.hpp>
#include <opengl/context.inl>
#include <opengl/scoped.hpp>

#include <external/tracy/Tracy.hpp>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Noggit::Rendering
{
  SunShadowMatrices compute_sun_shadow_matrices (
    glm::vec3 const& center
  , glm::vec3 const& sun_dir
  , float ortho_half_extent
  , float far_plane
  , int shadow_map_size
  )
  {
    SunShadowMatrices result;
    glm::vec3 const to_sun = glm::normalize (sun_dir);
    float const extent = std::max (ortho_half_extent, 64.f);
    float const far_dist = std::max (far_plane, extent * 2.f);
    int const map_size = std::max (shadow_map_size, 1);

    glm::vec3 const up = (std::abs (to_sun.y) > 0.95f)
                       ? glm::vec3 (0.f, 0.f, 1.f)
                       : glm::vec3 (0.f, 1.f, 0.f);

    // Light travels from the sun toward the scene (-to_sun). Place the light camera on the sun side.
    glm::vec3 const eye = center + to_sun * far_dist * 0.5f;
    glm::mat4 view = glm::lookAt (eye, center, up);
    glm::mat4 const projection = glm::ortho (
      -extent, extent
    , -extent, extent
    , 1.f, far_dist
    );

    float const texel_world = (2.f * extent) / static_cast<float> (map_size);
    glm::vec4 const center_ls = view * glm::vec4 (center, 1.f);
    glm::vec4 snapped_ls = center_ls;
    snapped_ls.x = std::floor (center_ls.x / texel_world + 0.5f) * texel_world;
    snapped_ls.y = std::floor (center_ls.y / texel_world + 0.5f) * texel_world;

    glm::vec4 const delta_ls = snapped_ls - center_ls;
    glm::vec3 const delta_world = glm::vec3 (glm::inverse (view) * glm::vec4 (delta_ls.x, delta_ls.y, delta_ls.z, 0.f));
    glm::vec3 const snapped_center = center + delta_world;
    glm::vec3 const snapped_eye = eye + delta_world;

    result.view = glm::lookAt (snapped_eye, snapped_center, up);
    result.projection = projection;
    result.view_proj = projection * result.view;

    glm::mat4 const bias (
      0.5f, 0.f, 0.f, 0.f
    , 0.f, 0.5f, 0.f, 0.f
    , 0.f, 0.f, 0.5f, 0.f
    , 0.5f, 0.5f, 0.5f, 1.f
    );
    result.view_proj_bias = bias * result.view_proj;
    return result;
  }

  void RealtimeGpuShadowMap::invalidate()
  {
    _valid = false;
    _pass_active = false;
    _last_snap_texel = glm::ivec2 (std::numeric_limits<int>::min(), std::numeric_limits<int>::min());
  }

  void RealtimeGpuShadowMap::ensure_resources()
  {
    if (_fbo && _depth_tex && _allocated_size == _size)
    {
      return;
    }

    if (_depth_tex)
    {
      gl.deleteTextures (1, &_depth_tex);
      _depth_tex = 0;
    }

    if (_dummy_color_tex)
    {
      gl.deleteTextures (1, &_dummy_color_tex);
      _dummy_color_tex = 0;
    }

    if (!_fbo)
    {
      gl.genFramebuffers (1, &_fbo);
    }

    if (!_depth_tex)
    {
      gl.genTextures (1, &_depth_tex);
      gl.bindTexture (GL_TEXTURE_2D, _depth_tex);
      gl.texParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      gl.texParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      gl.texParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
      gl.texParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
      gl.texParameteri (GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
      gl.texParameteri (GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
      float const border[] = {1.f, 1.f, 1.f, 1.f};
      gl.texParameterfv (GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
      gl.texImage2D (
        GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24
      , _size, _size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr
      );
      gl.bindTexture (GL_TEXTURE_2D, 0);
    }

    if (!_dummy_color_tex)
    {
      gl.genTextures (1, &_dummy_color_tex);
      gl.bindTexture (GL_TEXTURE_2D, _dummy_color_tex);
      gl.texImage2D (
        GL_TEXTURE_2D, 0, GL_RGBA8
      , _size, _size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr
      );
      gl.bindTexture (GL_TEXTURE_2D, 0);
    }

    gl.bindFramebuffer (GL_FRAMEBUFFER, _fbo);
    gl.framebufferTexture2D (
      GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _dummy_color_tex, 0
    );
    gl.framebufferTexture2D (
      GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, _depth_tex, 0
    );
    gl.bindFramebuffer (GL_FRAMEBUFFER, 0);
    _allocated_size = _size;
  }

  void RealtimeGpuShadowMap::prepare_frame (
    glm::vec3 const& center
  , glm::vec3 const& sun_dir
  , float ortho_half_extent
  , float far_plane
  )
  {
    ensure_resources();

    _sun_dir = glm::normalize (sun_dir);
    _matrices = compute_sun_shadow_matrices (
      center, _sun_dir, ortho_half_extent, far_plane, _size
    );

    glm::vec4 const snap_ndc = _matrices.view_proj * glm::vec4 (center, 1.f);
    glm::vec2 const snap01 (
      snap_ndc.x / snap_ndc.w * 0.5f + 0.5f
    , snap_ndc.y / snap_ndc.w * 0.5f + 0.5f
    );
    _last_snap_texel = glm::ivec2 (
      static_cast<int> (std::floor (snap01.x * static_cast<float> (_size)))
    , static_cast<int> (std::floor (snap01.y * static_cast<float> (_size)))
    );
  }

  void RealtimeGpuShadowMap::begin_depth_pass()
  {
    ZoneScopedN ("RealtimeGpuShadowMap::begin_depth_pass");

    gl.bindFramebuffer (GL_FRAMEBUFFER, _fbo);
    gl.viewport (0, 0, _size, _size);
    gl.clear (GL_DEPTH_BUFFER_BIT);
    gl.colorMask (GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    gl.enable (GL_DEPTH_TEST);
    gl.depthFunc (GL_LESS);
    gl.depthMask (GL_TRUE);
    gl.enable (GL_CULL_FACE);
    gl.enable (GL_POLYGON_OFFSET_FILL);
    gl.polygonOffset (1.5f, 2.f);

    _valid = true;
    _pass_active = true;
  }

  void RealtimeGpuShadowMap::end_depth_pass()
  {
    if (!_pass_active)
    {
      return;
    }

    gl.colorMask (GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.disable (GL_POLYGON_OFFSET_FILL);
    gl.bindFramebuffer (GL_FRAMEBUFFER, 0);
    _pass_active = false;
  }

  void RealtimeGpuShadowMap::unload()
  {
    end_depth_pass();
    invalidate();
  }

  RealtimeGpuShadowMap::ScopedDepthPass::ScopedDepthPass (RealtimeGpuShadowMap& map)
    : _map (&map)
  {
    _map->begin_depth_pass();
  }

  RealtimeGpuShadowMap::ScopedDepthPass::~ScopedDepthPass()
  {
    if (_map)
    {
      _map->end_depth_pass();
    }
  }
}
