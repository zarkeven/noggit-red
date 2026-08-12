// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/rendering/RealtimeTerrainShadowMask.hpp>
#include <noggit/MapChunk.h>
#include <noggit/Model.h>
#include <noggit/Selection.h>
#include <noggit/WMOInstance.h>
#include <noggit/World.h>

#include <math/ray.hpp>

#include <opengl/context.hpp>
#include <opengl/context.inl>

#include <external/tracy/Tracy.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace Noggit::Rendering
{
  namespace
  {
    constexpr int kMaxRaysPerFrame = 8192;
    constexpr int kMaxDynamicRaysPerFrame = 4096;
    constexpr int kMaxMaintenanceRaysPerFrame = 768;
    constexpr int kUploadIntervalFrames = 3;
    constexpr int kDynamicIntervalFrames = 3;
    constexpr int kMaintenanceIntervalFrames = 60;
    constexpr float kSunDirRebakeDot = 0.9998f;
    constexpr float kReanchorCameraFraction = 0.45f;
    constexpr float kOccluderPaddingTexels = 2.5f;

    std::uint64_t ground_cache_key (float wx, float wz)
    {
      auto const qx = static_cast<std::int32_t> (std::floor (wx * 4.f));
      auto const qz = static_cast<std::int32_t> (std::floor (wz * 4.f));
      return (static_cast<std::uint64_t> (static_cast<std::uint32_t> (qx)) << 32)
           | static_cast<std::uint64_t> (static_cast<std::uint32_t> (qz));
    }

    std::optional<float> sample_ground_y_at ( MapChunk* chunk
                                          , float wx
                                          , float wz
                                          , float reference_y
                                          )
    {
      if (!chunk)
      {
        return std::nullopt;
      }

      selection_result hits;
      glm::vec3 const high (wx, reference_y + 5000.f, wz);
      math::ray const down (high, glm::vec3 (0.f, -1.f, 0.f));
      if (chunk->intersect (down, &hits, true) && !hits.empty())
      {
        return std::get<selected_chunk_type> (hits[0].second).position.y;
      }

      glm::vec3 const low (wx, reference_y - 5000.f, wz);
      math::ray const up (low, glm::vec3 (0.f, 1.f, 0.f));
      if (chunk->intersect (up, &hits, true) && !hits.empty())
      {
        return std::get<selected_chunk_type> (hits[0].second).position.y;
      }

      return std::nullopt;
    }

    void world_xz_to_texel_range ( glm::vec2 const& origin_xz
                                 , float texel_world
                                 , int mask_size
                                 , float min_x
                                 , float max_x
                                 , float min_z
                                 , float max_z
                                 , int& out_ix0
                                 , int& out_ix1
                                 , int& out_iz0
                                 , int& out_iz1
                                 )
    {
      out_ix0 = std::clamp (
        static_cast<int> (std::floor ((min_x - origin_xz.x) / texel_world))
      , 0, mask_size - 1
      );
      out_ix1 = std::clamp (
        static_cast<int> (std::ceil ((max_x - origin_xz.x) / texel_world))
      , 0, mask_size - 1
      );
      out_iz0 = std::clamp (
        static_cast<int> (std::floor ((min_z - origin_xz.y) / texel_world))
      , 0, mask_size - 1
      );
      out_iz1 = std::clamp (
        static_cast<int> (std::ceil ((max_z - origin_xz.y) / texel_world))
      , 0, mask_size - 1
      );
    }
  }

  float RealtimeTerrainShadowMask::inv_texel_uv() const
  {
    return (_size > 0) ? (_inv_world_size / static_cast<float> (_size)) : 0.f;
  }

  void RealtimeTerrainShadowMask::flush_deferred_gl_delete()
  {
    if (!_defer_gl_delete || _tex == 0)
    {
      return;
    }

    gl.deleteTextures (1, &_tex);
    _tex = 0;
    _defer_gl_delete = false;
    _pixels.clear();
    _scratch.clear();
    _visit_scratch.clear();
    _bake_order.clear();
    _occluder_bounds.clear();
    _ground_y_cache.clear();
    _allocated_size = 0;
    _bake_complete = false;
    _gpu_dirty = false;
  }

  void RealtimeTerrainShadowMask::invalidate()
  {
    _bake_complete = false;
    _sun_dir_initialized = false;
    _bake_cursor = 0;
    _gpu_dirty = false;
    _snap_origin = glm::ivec2 (std::numeric_limits<int>::min(), std::numeric_limits<int>::min());

    if (_tex != 0)
    {
      _defer_gl_delete = true;
    }
  }

  void RealtimeTerrainShadowMask::reset_dirty_rect()
  {
    _dirty_min_ix = _size;
    _dirty_max_ix = -1;
    _dirty_min_iz = _size;
    _dirty_max_iz = -1;
  }

  void RealtimeTerrainShadowMask::note_dirty_texel (int ix, int iz)
  {
    _dirty_min_ix = std::min (_dirty_min_ix, ix);
    _dirty_max_ix = std::max (_dirty_max_ix, ix);
    _dirty_min_iz = std::min (_dirty_min_iz, iz);
    _dirty_max_iz = std::max (_dirty_max_iz, iz);
    _gpu_dirty = true;
  }

  void RealtimeTerrainShadowMask::ensure_texture()
  {
    flush_deferred_gl_delete();

    if (_tex && _allocated_size == _size)
    {
      return;
    }

    if (_tex)
    {
      gl.deleteTextures (1, &_tex);
      _tex = 0;
      _allocated_size = 0;
    }

    gl.genTextures (1, &_tex);
    gl.bindTexture (GL_TEXTURE_2D, _tex);
    gl.texParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.texParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.texParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    gl.texParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float const border[] = {1.f, 1.f, 1.f, 1.f};
    gl.texParameterfv (GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    std::size_t const count = static_cast<std::size_t> (_size) * static_cast<std::size_t> (_size);
    _pixels.assign (count, 255);
    _scratch.assign (count, 255);

    gl.texImage2D (GL_TEXTURE_2D, 0, GL_R8, _size, _size, 0, GL_RED, GL_UNSIGNED_BYTE, _pixels.data());
    gl.bindTexture (GL_TEXTURE_2D, 0);
    _allocated_size = _size;
  }

  void RealtimeTerrainShadowMask::build_bake_order_spiral (glm::vec3 const& camera_pos)
  {
    int const total_texels = _size * _size;
    _bake_order.assign (static_cast<std::size_t> (total_texels), -1);

    int const cx = std::clamp (
      static_cast<int> (std::floor ((camera_pos.x - _origin_xz.x) / _texel_world))
    , 0, _size - 1
    );
    int const cz = std::clamp (
      static_cast<int> (std::floor ((camera_pos.z - _origin_xz.y) / _texel_world))
    , 0, _size - 1
    );

  if (_visit_scratch.size() != static_cast<std::size_t> (total_texels))
    {
      _visit_scratch.assign (static_cast<std::size_t> (total_texels), 0);
    }
    else
    {
      std::fill (_visit_scratch.begin(), _visit_scratch.end(), 0);
    }

    _bake_order.clear();
    _bake_order.reserve (static_cast<std::size_t> (total_texels));

    auto const push = [&] (int ix, int iz)
    {
      if (ix < 0 || ix >= _size || iz < 0 || iz >= _size)
      {
        return;
      }
      std::size_t const idx = static_cast<std::size_t> (iz * _size + ix);
      if (_visit_scratch[idx])
      {
        return;
      }
      _visit_scratch[idx] = 1;
      _bake_order.push_back (static_cast<int> (idx));
    };

    push (cx, cz);
    for (int r = 1; r < _size && static_cast<int> (_bake_order.size()) < total_texels; ++r)
    {
      for (int dz = -r; dz <= r; ++dz)
      {
        push (cx + r, cz + dz);
        push (cx - r, cz + dz);
      }
      for (int dx = -r + 1; dx < r; ++dx)
      {
        push (cx + dx, cz + r);
        push (cx + dx, cz - r);
      }
    }
  }

  void RealtimeTerrainShadowMask::rebuild_occluder_bounds (
    tsl::robin_map<Model*, std::vector<glm::mat4x4>> const* culled_models
  , std::vector<WMOInstance*> const* culled_wmos
  , WorldRenderParams const& render_settings
  )
  {
    _occluder_bounds.clear();

    if (render_settings.draw_models && culled_models)
    {
      for (auto const& pair : *culled_models)
      {
        Model* model = pair.first;
        if (!model || !model->finishedLoading() || model->loading_failed())
        {
          continue;
        }

        for (glm::mat4x4 const& transform : pair.second)
        {
          glm::vec3 const pos (transform[3]);
          float const scale = std::max (
            glm::length (glm::vec3 (transform[0]))
          , std::max (glm::length (glm::vec3 (transform[1])), glm::length (glm::vec3 (transform[2])))
          );
          float const radius = model->bounding_box_radius * scale + _texel_world * kOccluderPaddingTexels;
          _occluder_bounds.push_back ({
            pos.x - radius, pos.x + radius, pos.z - radius, pos.z + radius
          });
        }
      }
    }

    if (render_settings.draw_wmo && culled_wmos)
    {
      float const pad = _texel_world * kOccluderPaddingTexels;
      for (WMOInstance* instance : *culled_wmos)
      {
        if (!instance)
        {
          continue;
        }
        instance->ensureExtents();
        std::array<glm::vec3, 2> const& ext = instance->getExtents();
        _occluder_bounds.push_back ({
          ext[0].x - pad, ext[1].x + pad, ext[0].z - pad, ext[1].z + pad
        });
      }
    }
  }

  bool RealtimeTerrainShadowMask::texel_near_occluder (float wx, float wz) const
  {
    for (OccluderAabb const& box : _occluder_bounds)
    {
      if (wx >= box.min_x && wx <= box.max_x && wz >= box.min_z && wz <= box.max_z)
      {
        return true;
      }
    }
    return false;
  }

  std::optional<float> RealtimeTerrainShadowMask::cached_ground_y (
    World* world, float wx, float wz, float reference_y
  )
  {
    std::uint64_t const key = ground_cache_key (wx, wz);
    if (auto const it = _ground_y_cache.find (key); it != _ground_y_cache.end())
    {
      return it->second;
    }

    MapChunk* const chunk = world->getChunkAt (glm::vec3 (wx, reference_y, wz));
    std::optional<float> const y = sample_ground_y_at (chunk, wx, wz, reference_y);
    if (y)
    {
      _ground_y_cache[key] = *y;
    }
    return y;
  }

  void RealtimeTerrainShadowMask::upload_if_dirty()
  {
    if (!_gpu_dirty || !_tex)
    {
      return;
    }

    std::vector<std::uint8_t> const& src = _bake_complete ? _pixels : _scratch;
    if (src.empty())
    {
      return;
    }

    gl.bindTexture (GL_TEXTURE_2D, _tex);

    bool const partial = _dirty_max_ix >= _dirty_min_ix && _dirty_max_iz >= _dirty_min_iz;
    if (partial)
    {
      int const w = _dirty_max_ix - _dirty_min_ix + 1;
      int const h = _dirty_max_iz - _dirty_min_iz + 1;
      (void)w;
      (void)h;
      gl.texSubImage2D (
        GL_TEXTURE_2D, 0, 0, 0, _size, _size, GL_RED, GL_UNSIGNED_BYTE, src.data()
      );
    }
    else
    {
      gl.texSubImage2D (
        GL_TEXTURE_2D, 0, 0, 0, _size, _size, GL_RED, GL_UNSIGNED_BYTE, src.data()
      );
    }

    gl.bindTexture (GL_TEXTURE_2D, 0);
    _gpu_dirty = false;
    reset_dirty_rect();
  }

  void RealtimeTerrainShadowMask::begin_rebake ( glm::ivec2 const& snap_origin
                                             , glm::vec3 const& sun_dir
                                             )
  {
    _snap_origin = snap_origin;
    _last_sun_dir = sun_dir;
    _sun_dir_initialized = true;
    _origin_xz = glm::vec2 (
      static_cast<float> (snap_origin.x) * _texel_world - _world_size * 0.5f
    , static_cast<float> (snap_origin.y) * _texel_world - _world_size * 0.5f
    );
    _inv_world_size = (_world_size > 1e-5f) ? (1.f / _world_size) : 0.f;
    _bake_cursor = 0;
    _bake_complete = false;
    _ground_y_cache.clear();
    reset_dirty_rect();
    _gpu_dirty = true;

    ensure_texture();

    if (!_pixels.empty() && _pixels.size() == _scratch.size())
    {
      _scratch = _pixels;
    }
    else
    {
      std::fill (_scratch.begin(), _scratch.end(), 255);
    }
  }

  bool RealtimeTerrainShadowMask::bake_texel ( World* world
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
                                            )
  {
    std::size_t const idx = static_cast<std::size_t> (iz * _size + ix);
    float const wx = _origin_xz.x + (static_cast<float> (ix) + 0.5f) * _texel_world;
    float const wz = _origin_xz.y + (static_cast<float> (iz) + 0.5f) * _texel_world;

    if (_occluder_bounds.empty())
    {
      if (buffer[idx] != 255)
      {
        buffer[idx] = 255;
        note_dirty_texel (ix, iz);
      }
      return false;
    }

    if (!texel_near_occluder (wx, wz))
    {
      if (buffer[idx] != 255)
      {
        buffer[idx] = 255;
        note_dirty_texel (ix, iz);
      }
      return false;
    }

    std::optional<float> const ground_y = cached_ground_y (world, wx, wz, camera_pos.y);
    if (!ground_y)
    {
      if (buffer[idx] != 255)
      {
        buffer[idx] = 255;
        note_dirty_texel (ix, iz);
      }
      return false;
    }

    glm::vec3 const ground (wx, *ground_y, wz);
    bool const occluded = world->isSunOccluded (
      ground
    , sun_dir
    , max_shadow_distance
    , model_view
    , animate_models
    , render_settings.draw_models
    , render_settings.draw_wmo
    , render_settings.draw_hidden_models
    , render_settings.draw_wmo_exterior
    , culled_models
    , culled_wmos
    );

    std::uint8_t const value = occluded ? 0 : 255;
    if (buffer[idx] != value)
    {
      buffer[idx] = value;
      note_dirty_texel (ix, iz);
    }
    return value == 0;
  }

  bool RealtimeTerrainShadowMask::refresh_near_occluders ( World* world
                                                         , glm::vec3 const& camera_pos
                                                         , glm::vec3 const& sun_dir
                                                         , float max_shadow_distance
                                                         , glm::mat4x4 const& model_view
                                                         , WorldRenderParams const& render_settings
                                                         , bool animate_models
                                                         , tsl::robin_map<Model*, std::vector<glm::mat4x4>> const* culled_models
                                                         , std::vector<WMOInstance*> const* culled_wmos
                                                         , int ray_budget
                                                         )
  {
    if (_pixels.empty() || _occluder_bounds.empty())
    {
      return false;
    }

    std::size_t const grid_count = static_cast<std::size_t> (_size) * static_cast<std::size_t> (_size);
    if (_visit_scratch.size() != grid_count)
    {
      _visit_scratch.assign (grid_count, 0);
    }
    else
    {
      std::fill (_visit_scratch.begin(), _visit_scratch.end(), 0);
    }

    int budget = std::max (ray_budget, 0);
    bool changed = false;

    auto const update_range = [&] ( int ix0
                                  , int ix1
                                  , int iz0
                                  , int iz1
                                  )
    {
      for (int iz = iz0; iz <= iz1 && budget > 0; ++iz)
      {
        for (int ix = ix0; ix <= ix1 && budget > 0; ++ix)
        {
          std::size_t const idx = static_cast<std::size_t> (iz * _size + ix);
          if (_visit_scratch[idx])
          {
            continue;
          }
          _visit_scratch[idx] = 1;
          --budget;

          std::uint8_t const before = _pixels[idx];
          if (bake_texel (world, ix, iz, camera_pos, sun_dir, max_shadow_distance
                        , model_view, render_settings, animate_models
                        , culled_models, culled_wmos, _pixels)
              && _pixels[idx] != before)
          {
            changed = true;
          }
        }
      }
    };

    for (OccluderAabb const& box : _occluder_bounds)
    {
      int ix0, ix1, iz0, iz1;
      world_xz_to_texel_range (
        _origin_xz, _texel_world, _size
      , box.min_x, box.max_x, box.min_z, box.max_z
      , ix0, ix1, iz0, iz1
      );
      update_range (ix0, ix1, iz0, iz1);
    }

    return changed;
  }

  bool RealtimeTerrainShadowMask::update ( World* world
                                         , glm::vec3 const& camera_pos
                                         , glm::vec3 const& sun_dir
                                         , float max_shadow_distance
                                         , glm::mat4x4 const& model_view
                                         , WorldRenderParams const& render_settings
                                         , bool animate_models
                                         , tsl::robin_map<Model*, std::vector<glm::mat4x4>> const* culled_models
                                         , std::vector<WMOInstance*> const* culled_wmos
                                         )
  {
    ZoneScopedN ("RealtimeTerrainShadowMask::update");

    flush_deferred_gl_delete();

    if (render_settings.minimap_render
        || render_settings.display_mode != display_mode::in_3D
        || !render_settings.draw_realtime_shadows
        || !render_settings.draw_terrain)
    {
      _bake_complete = false;
      return false;
    }

    _texel_world = _world_size / static_cast<float> (_size);
    max_shadow_distance = std::min (max_shadow_distance, _world_size * 1.05f);

    rebuild_occluder_bounds (culled_models, culled_wmos, render_settings);

    glm::ivec2 const desired_snap (
      static_cast<int> (std::floor (camera_pos.x / _texel_world))
    , static_cast<int> (std::floor (camera_pos.z / _texel_world))
    );

    glm::vec2 const anchor_world (
      static_cast<float> (_snap_origin.x) * _texel_world
    , static_cast<float> (_snap_origin.y) * _texel_world
    );
    glm::vec2 const cam_xz (camera_pos.x, camera_pos.z);
    float const anchor_drift = glm::length (cam_xz - anchor_world);

    bool const sun_changed = _sun_dir_initialized
                          && _bake_complete
                          && (glm::dot (sun_dir, _last_sun_dir) < kSunDirRebakeDot);

    bool const anchor_moved = _sun_dir_initialized
                           && anchor_drift > _world_size * kReanchorCameraFraction;

    bool const has_occluders = !_occluder_bounds.empty();

    if (_bake_complete && !sun_changed && !anchor_moved)
    {
      ++_frame_counter;

      bool const due_dynamic = animate_models
                            && has_occluders
                            && (_frame_counter % kDynamicIntervalFrames) == 0;
      bool const due_maintenance = !animate_models
                               && has_occluders
                               && (_frame_counter % kMaintenanceIntervalFrames) == 0;

      if (!due_dynamic && !due_maintenance)
      {
        return true;
      }

      if (due_dynamic)
      {
        refresh_near_occluders (
          world, camera_pos, sun_dir, max_shadow_distance, model_view
        , render_settings, animate_models, culled_models, culled_wmos
        , kMaxDynamicRaysPerFrame
        );
      }
      else
      {
        refresh_near_occluders (
          world, camera_pos, sun_dir, max_shadow_distance, model_view
        , render_settings, false, culled_models, culled_wmos
        , kMaxMaintenanceRaysPerFrame
        );
      }

      if (_gpu_dirty)
      {
        upload_if_dirty();
      }

      return true;
    }

    ++_frame_counter;

    if (!_bake_complete)
    {
      if (_bake_cursor == 0)
      {
        begin_rebake (desired_snap, sun_dir);
        build_bake_order_spiral (camera_pos);
      }

      ensure_texture();

      int const total_texels = _size * _size;
      int const rays_budget = std::min (kMaxRaysPerFrame, total_texels - _bake_cursor);
      int processed = 0;

      while (processed < rays_budget && _bake_cursor < total_texels)
      {
        int const texel_index = _bake_order.empty()
          ? _bake_cursor
          : _bake_order[static_cast<std::size_t> (_bake_cursor)];
        int const ix = texel_index % _size;
        int const iz = texel_index / _size;
        ++_bake_cursor;
        ++processed;

        bake_texel (
          world, ix, iz, camera_pos, sun_dir, max_shadow_distance
        , model_view, render_settings
        , false // static poses for batched full-grid bake
        , culled_models, culled_wmos, _scratch
        );
      }

      bool const finished = _bake_cursor >= total_texels;
      if (finished)
      {
        _pixels = _scratch;
        _bake_complete = true;
        reset_dirty_rect();
        _gpu_dirty = true;
        upload_if_dirty();
      }
      else if (_gpu_dirty && (_frame_counter % kUploadIntervalFrames) == 0)
      {
        upload_if_dirty();
      }
    }
    else if (sun_changed || anchor_moved)
    {
      begin_rebake (desired_snap, sun_dir);
    }

    return _bake_complete;
  }
}
