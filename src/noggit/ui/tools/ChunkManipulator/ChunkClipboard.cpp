// This file is part of Noggit3, licensed under GNU General Public License (version 3).


#include "ChunkClipboard.hpp"
#include <noggit/Action.hpp>
#include <noggit/Alphamap.hpp>
#include <noggit/ChunkWater.hpp>
#include <noggit/Log.h>
#include <noggit/MapChunk.h>
#include <noggit/MapTile.h>
#include <noggit/texture_set.hpp>
#include <noggit/World.h>
#include <algorithm>
#include <noggit/liquid_layer.hpp>
#include <noggit/World.inl>

#include <cassert>

using namespace Noggit::Ui::Tools::ChunkManipulator;

ChunkClipboard::ChunkClipboard(World* world, QObject* parent)
: QObject(parent)
, _world(world)
, _copy_flags(chunk_copy_flags_all())
{
}

float ChunkClipboard::pasteTerrainHeightOffset() const
{
  return _paste_terrain_height_offset;
}

void ChunkClipboard::setPasteTerrainHeightOffset(float y)
{
  _paste_terrain_height_offset = y;
}

void ChunkClipboard::selectRange(glm::vec3 const& cursor_pos, float radius, ChunkSelectionMode mode)
{
  switch (mode)
  {
    case ChunkSelectionMode::SELECT:
    {
      _world->for_all_chunks_in_range(cursor_pos, radius, [this](MapChunk* chunk) -> bool
      {
        _selected_chunks.emplace(SelectedChunkIndex{ chunk->mt->index,
                                                    static_cast<unsigned>(chunk->px), static_cast<unsigned>(chunk->py)});
        return true;
      }
      );
      break;
    }
    case ChunkSelectionMode::DESELECT:
    {
      _world->for_all_chunks_in_range(cursor_pos, radius, [this](MapChunk* chunk) -> bool
      {
        _selected_chunks.erase(SelectedChunkIndex{ chunk->mt->index,
                                                    static_cast<unsigned>(chunk->px), static_cast<unsigned>(chunk->py)});
        return true;
      });
      break;
    }
    default:
      assert(false);
  }

  emit selectionChanged(_selected_chunks);
}

void ChunkClipboard::selectChunk(glm::vec3 const& pos, ChunkSelectionMode mode)
{
  switch(mode)
  {
    case ChunkSelectionMode::SELECT:
    {
      _world->for_chunk_at(pos, [this](MapChunk* chunk) -> bool
      {
        _selected_chunks.emplace(SelectedChunkIndex{ chunk->mt->index,
                                                    static_cast<unsigned>(chunk->px), static_cast<unsigned>(chunk->py)});
        return true;
      });
      break;
    }
    case ChunkSelectionMode::DESELECT:
    {
      _world->for_chunk_at(pos, [this](MapChunk* chunk) -> bool
      {
        _selected_chunks.erase(SelectedChunkIndex{ chunk->mt->index,
                                                    static_cast<unsigned>(chunk->px), static_cast<unsigned>(chunk->py)});
        return true;
      });
      break;
    }
    default:
      assert(false);
  }

  emit selectionChanged(_selected_chunks);
}

void ChunkClipboard::selectChunk(TileIndex const& tile_index, unsigned x, unsigned z, ChunkSelectionMode mode)
{
   SelectedChunkIndex index {tile_index,static_cast<unsigned>(x), static_cast<unsigned>(z)};
   assert(index.tile_index.is_valid());

   if (!_world->mapIndex.hasTile(index.tile_index))
     return;

   switch (mode)
   {
     case ChunkSelectionMode::SELECT:
     {
       _selected_chunks.emplace(index);
       break;
     }
     case ChunkSelectionMode::DESELECT:
     {
       _selected_chunks.erase(index);
       break;
     }
     default:
       assert(false);
   }

  emit selectionChanged(_selected_chunks);
}

void ChunkClipboard::copySelected(glm::vec3 const& pos)
{
  _cached_chunks.clear();
  _last_copy_pos = pos;
  _has_last_copy_pos = true;

  ChunkCopyFlags const flags = _copy_flags;

  MapTile* pivot_tile = _world->mapIndex.loadTile({pos});

  if (!pivot_tile)
    return;

  pivot_tile->wait_until_loaded();

  MapChunk* pivot_chunk = _world->getChunkAt(pos);
  if (!pivot_chunk)
    return;

  for (auto const& index : _selected_chunks)
  {
    ChunkCache chunk_cache;
    MapTile* tile = _world->mapIndex.loadTile(index.tile_index);
    if (!tile)
      continue;
    tile->wait_until_loaded();
    MapChunk* chunk = tile->getChunk(index.x, index.z);

    if (chunk_copy_flags_test(flags, ChunkCopyFlags::TERRAIN))
    {
      chunk_cache.terrain_height = std::array<char, 145 * 3 * sizeof(float)>{};
      std::memcpy(chunk_cache.terrain_height->data(), &chunk->mVertices, 145 * 3 * sizeof(float));
    }

    if (chunk_copy_flags_test(flags, ChunkCopyFlags::VERTEX_COLORS))
    {
      chunk_cache.vertex_colors = std::array<char, 145 * 3 * sizeof(float)>{};
      std::memcpy(chunk_cache.vertex_colors->data(), &chunk->mccv, 145 * 3 * sizeof(float));
    }

    if (chunk_copy_flags_test(flags, ChunkCopyFlags::SHADOWS))
    {
      chunk_cache.shadows = std::array<std::uint8_t, 64 * 64>{};
      std::memcpy(chunk_cache.shadows->data(), &chunk->_shadow_map, 64 * 64 * sizeof(std::uint8_t));
    }

    if (chunk_copy_flags_test(flags, ChunkCopyFlags::LIQUID))
    {
      std::vector<ChunkLiquidLayerCacheEntry> layers;
      auto const& src_layers = *chunk->liquid_chunk()->getLayers();
      layers.reserve(src_layers.size());
      for (auto const& layer : src_layers)
      {
        ChunkLiquidLayerCacheEntry e;
        e.liquid_id = layer.liquidID();
        // liquid_layer doesn't provide const accessors, but reading is safe here.
        auto& layer_nc = const_cast<liquid_layer&>(layer);
        e.subchunks = layer_nc.getSubchunks();
        auto const& verts = layer_nc.getVertices();
        for (int i = 0; i < 9 * 9; ++i)
        {
          e.vertex_data[static_cast<std::size_t>(i) * 4 + 0] = verts[static_cast<std::size_t>(i)].position.y;
          e.vertex_data[static_cast<std::size_t>(i) * 4 + 1] = verts[static_cast<std::size_t>(i)].depth;
          e.vertex_data[static_cast<std::size_t>(i) * 4 + 2] = verts[static_cast<std::size_t>(i)].uv.x;
          e.vertex_data[static_cast<std::size_t>(i) * 4 + 3] = verts[static_cast<std::size_t>(i)].uv.y;
        }
        layers.emplace_back(std::move(e));
      }
      chunk_cache.liquid_layers = std::move(layers);
    }

    if (chunk_copy_flags_test(flags, ChunkCopyFlags::TEXTURES))
    {
      ChunkTextureCache cache;
      auto texture_set = chunk->getTextureSet();

      cache.n_textures = texture_set->num();
      const auto& sourceAlphamaps = *texture_set->getAlphamaps();
      for (size_t i = 0; i < MAX_ALPHAMAPS; ++i)
      {
          if (sourceAlphamaps[i])
              cache.alphamaps[i] = std::make_unique<Alphamap>(*sourceAlphamaps[i]);
          else
              cache.alphamaps[i].reset();
      }

      const auto& source_temp_alphas = texture_set->getTempAlphamaps();
      if (source_temp_alphas)
          cache.tmp_edit_values = std::make_unique<tmp_edit_alpha_values>(*source_temp_alphas);
      else
          cache.tmp_edit_values.reset();

      std::memcpy(&cache.layers_info, texture_set->getMCLYEntries(), sizeof(layer_info) * 4);

      for (int i = 0; i < static_cast<int>(cache.n_textures); ++i)
      {
        cache.textures.push_back(texture_set->filename(i));
      }

      chunk_cache.textures = std::move(cache);
    }

    if (chunk_copy_flags_test(flags, ChunkCopyFlags::HOLES))
    {
      chunk_cache.holes = chunk->holes;
    }

    if (chunk_copy_flags_test(flags, ChunkCopyFlags::FLAGS))
    {
      chunk_cache.flags = chunk->header_flags;
    }

    if (chunk_copy_flags_test(flags, ChunkCopyFlags::AREA_ID))
    {
      chunk_cache.area_id = static_cast<int>(chunk->areaID);
    }

    TileIndex const pivot_ti = pivot_chunk->mt->index;
    int const pivot_gx = static_cast<int>(pivot_ti.x) * 16 + pivot_chunk->px;
    int const pivot_gz = static_cast<int>(pivot_ti.z) * 16 + pivot_chunk->py;
    int const gx = static_cast<int>(index.tile_index.x) * 16 + static_cast<int>(index.x);
    int const gz = static_cast<int>(index.tile_index.z) * 16 + static_cast<int>(index.z);

    SelectedChunkIndex const base{ index.tile_index, index.x, index.z };
    _cached_chunks.emplace_back(
        SelectedChunkIndexRelative{ base, gx - pivot_gx, gz - pivot_gz },
        std::move(chunk_cache));
  }

  if (chunk_copy_flags_test(flags, ChunkCopyFlags::POINT_LIGHTS))
  {
    // Cache point lights that fall within the selected chunks.
    std::set<std::pair<std::uint16_t, std::uint16_t>> selected_chunk_global;
    // "No compromises" placement, but pivot must match chunk paste pivot.
    // Chunk paste snaps to the pivot chunk grid, so store offsets relative to the pivot chunk's world origin.
    // Paste reconstructs as: new_position = paste_pivot_chunk_origin + rel_position
    glm::vec3 pivot_world = pos;
    if (pivot_chunk)
    {
      pivot_world.x = pivot_chunk->xbase;
      pivot_world.z = pivot_chunk->zbase;
    }
    for (auto const& idx : _selected_chunks)
    {
      selected_chunk_global.emplace(
        static_cast<std::uint16_t>(idx.tile_index.x * 16u + idx.x),
        static_cast<std::uint16_t>(idx.tile_index.z * 16u + idx.z));
    }

    std::vector<ChunkPointLightCacheEntry> lights_cache;
    auto const& lights = _world->pointLights();
    lights_cache.reserve(lights.size());

    for (auto const& L : lights)
    {
      std::uint16_t adt_x = 0, adt_z = 0;
      int mcnk_x = 0, mcnk_z = 0;
      World::worldPosToAdtMcnk(L.position, adt_x, adt_z, mcnk_x, mcnk_z);
      if (!selected_chunk_global.contains(std::make_pair(
            static_cast<std::uint16_t>(adt_x * 16u + static_cast<std::uint16_t>(mcnk_x)),
            static_cast<std::uint16_t>(adt_z * 16u + static_cast<std::uint16_t>(mcnk_z)))))
      {
        continue;
      }

      ChunkPointLightCacheEntry e;
      e.rel_position = L.position - pivot_world;
      e.color = L.color;
      e.attenuation_start = L.attenuation_start;
      e.attenuation_end = L.attenuation_end;
      e.intensity = L.intensity;
      e.flicker_mode = L.flicker_mode;
      e.flicker_intensity = L.flicker_intensity;
      e.flicker_speed = L.flicker_speed;
      e.flicker_seed = L.flicker_seed;
      e.light_type = L.light_type;
      e.rotation_radians = L.rotation_radians;
      e.spotlight_radius = L.spotlight_radius;
      e.spot_gizmo_scale = L.spot_gizmo_scale;
      e.inner_angle = L.inner_angle;
      e.outer_angle = L.outer_angle;
      e.mlta_active = L.mlta_active;
      e.mlta_amplitude = L.mlta_amplitude;
      e.mlta_frequency = L.mlta_frequency;
      e.mlta_function = L.mlta_function;
      e.mlta_index = L.mlta_index;
      e.texture_index = L.texture_index;
      e.cookie_file_data_id = L.cookie_file_data_id;
      lights_cache.emplace_back(e);
    }

    // Store point light cache on the first cached entry (clipboard-global for now).
    if (!_cached_chunks.empty())
    {
      _cached_chunks.front().second.point_lights = std::move(lights_cache);
    }
  }
}

void ChunkClipboard::clearSelection()
{
  _selected_chunks.clear();
  // Keep preview/cache consistent with the current quick selection.
  _cached_chunks.clear();
  emit selectionCleared();
  emit selectionChanged(_selected_chunks);
}

void ChunkClipboard::pasteSelection(glm::vec3 const& pos, ChunkPasteFlags flags, Noggit::Action* undo_action)
{
  (void)flags;

  if (_cached_chunks.empty())
    return;

  MapChunk* pivot_chunk = _world->getChunkAt(pos);
  if (!pivot_chunk)
    return;

  glm::vec3 pivot_world = pos;
  pivot_world.x = pivot_chunk->xbase;
  pivot_world.z = pivot_chunk->zbase;

  TileIndex const pivot_ti = pivot_chunk->mt->index;
  int const pivot_gx = static_cast<int>(pivot_ti.x) * 16 + pivot_chunk->px;
  int const pivot_gz = static_cast<int>(pivot_ti.z) * 16 + pivot_chunk->py;

  float const paste_dy = _paste_terrain_height_offset;

  for (auto const& entry : _cached_chunks)
  {
    SelectedChunkIndexRelative const& rel = entry.first;
    ChunkCache const& cache = entry.second;

    int const tgx = pivot_gx + rel.rel_x;
    int const tgz = pivot_gz + rel.rel_z;
    if (tgx < 0 || tgz < 0)
      continue;

    std::size_t const tile_x = static_cast<std::size_t>(tgx / 16);
    std::size_t const tile_z = static_cast<std::size_t>(tgz / 16);
    TileIndex const ti{ tile_x, tile_z };
    if (!ti.is_valid() || !_world->mapIndex.hasTile(ti))
      continue;

    unsigned const cx = static_cast<unsigned>(tgx % 16);
    unsigned const cz = static_cast<unsigned>(tgz % 16);

    MapTile* tile = _world->mapIndex.loadTile(ti);
    if (!tile)
      continue;
    tile->wait_until_loaded();

    MapChunk* chunk = tile->getChunk(cx, cz);
    if (!chunk)
      continue;

    if (cache.terrain_height)
    {
      if (undo_action)
        undo_action->registerChunkTerrainChange(chunk);
      std::memcpy(&chunk->mVertices, cache.terrain_height->data(), 145 * 3 * sizeof(float));
      if (paste_dy != 0.f)
      {
        for (int i = 0; i < mapbufsize; ++i)
        {
          chunk->mVertices[i].y += paste_dy;
        }
      }
      chunk->registerChunkUpdate(ChunkUpdateFlags::VERTEX);
      chunk->updateVerticesData();
      chunk->recalcNorms();
    }

    if (cache.vertex_colors)
    {
      if (undo_action)
        undo_action->registerChunkVertexColorChange(chunk);
      if (!chunk->hasColors())
        chunk->initMCCV();
      std::memcpy(&chunk->mccv, cache.vertex_colors->data(), 145 * 3 * sizeof(float));
      chunk->registerChunkUpdate(ChunkUpdateFlags::MCCV);
      chunk->update_vertex_colors();
    }

    if (cache.shadows)
    {
      if (undo_action)
        undo_action->registerChunkShadowChange(chunk);
      std::memcpy(&chunk->_shadow_map, cache.shadows->data(), 64 * 64 * sizeof(std::uint8_t));
      chunk->registerChunkUpdate(ChunkUpdateFlags::SHADOW);
      chunk->update_shadows();
    }

    if (cache.liquid_layers)
    {
      if (undo_action)
        undo_action->registerChunkLiquidChange(chunk);
      auto* liquid_chunk = chunk->liquid_chunk();
      auto& dst_layers = *liquid_chunk->getLayers();
      dst_layers.clear();
      dst_layers.reserve(cache.liquid_layers->size());

      glm::vec3 const base(chunk->xbase, 0.f, chunk->zbase);
      for (auto const& src : *cache.liquid_layers)
      {
        float const h0 = src.vertex_data[0] + paste_dy;
        liquid_layer layer(liquid_chunk, base, h0, src.liquid_id);

        // Restore subchunks.
        layer.clear();
        for (int z = 0; z < 8; ++z)
        {
          for (int x = 0; x < 8; ++x)
          {
            bool const has = ((src.subchunks >> (z * 8 + x)) & 1ull) != 0;
            layer.setSubchunk(x, z, has);
          }
        }

        // Restore per-vertex height/depth/uv.
        auto& verts = layer.getVertices();
        for (int i = 0; i < 9 * 9; ++i)
        {
          verts[static_cast<std::size_t>(i)].position.y = src.vertex_data[static_cast<std::size_t>(i) * 4 + 0] + paste_dy;
          verts[static_cast<std::size_t>(i)].depth = src.vertex_data[static_cast<std::size_t>(i) * 4 + 1];
          verts[static_cast<std::size_t>(i)].uv.x = src.vertex_data[static_cast<std::size_t>(i) * 4 + 2];
          verts[static_cast<std::size_t>(i)].uv.y = src.vertex_data[static_cast<std::size_t>(i) * 4 + 3];
        }

        dst_layers.emplace_back(std::move(layer));
      }
      liquid_chunk->update_layers();
      liquid_chunk->tagUpdate();
      // Force tile-level water renderer/extents refresh (otherwise it may only show after ADT reload).
      if (chunk->mt)
      {
        chunk->mt->Water.tagExtents(true);
        chunk->mt->Water.tagUpdate();
      }
    }

    if (cache.textures)
    {
      if (undo_action)
        undo_action->registerChunkTextureChange(chunk);

      auto* texture_set = chunk->getTextureSet();
      texture_set->setAlphamaps(cache.textures->alphamaps);

      if (cache.textures->tmp_edit_values)
        texture_set->getTempAlphamaps() = std::make_unique<tmp_edit_alpha_values>(*cache.textures->tmp_edit_values);
      else
        texture_set->getTempAlphamaps().reset();

      std::memcpy(texture_set->getMCLYEntries(), &cache.textures->layers_info, sizeof(layer_info) * 4);
      texture_set->setNTextures(cache.textures->n_textures);

      auto* textures = texture_set->getTextures();
      textures->clear();
      textures->reserve(cache.textures->n_textures);
      for (auto const& filename : cache.textures->textures)
      {
        textures->emplace_back(filename, Noggit::NoggitRenderContext::MAP_VIEW);
      }

      texture_set->markDirty();
      texture_set->apply_alpha_changes();
      chunk->registerChunkUpdate(ChunkUpdateFlags::FLAGS); // for texture anim flags
    }

    if (cache.holes)
    {
      if (undo_action)
        undo_action->registerChunkHoleChange(chunk);
      chunk->holes = *cache.holes;
      chunk->registerChunkUpdate(ChunkUpdateFlags::HOLES);
    }

    if (cache.area_id)
    {
      if (undo_action)
        undo_action->registerChunkAreaIDChange(chunk);
      chunk->setAreaID(*cache.area_id);
    }

    if (cache.flags)
    {
      if (undo_action)
        undo_action->registerChunkFlagChange(chunk);
      chunk->header_flags = *cache.flags;
      chunk->registerChunkUpdate(ChunkUpdateFlags::FLAGS);
    }

    // Paste point lights once (stored on first entry).
    if (cache.point_lights)
    {
      if (undo_action)
        undo_action->registerPointLightsChange();

      auto& lights = _world->pointLights();
      std::uint32_t next_id = 1u;
      for (auto const& L : lights)
        next_id = std::max(next_id, L.id + 1u);

      for (auto const& e : *cache.point_lights)
      {
        World::PointLight L;
        L.id = next_id++;
        L.position = pivot_world + e.rel_position;
        L.position.y += paste_dy;
        L.color = e.color;
        L.attenuation_start = e.attenuation_start;
        L.attenuation_end = e.attenuation_end;
        L.intensity = e.intensity;
        L.flicker_mode = e.flicker_mode;
        L.flicker_intensity = e.flicker_intensity;
        L.flicker_speed = e.flicker_speed;
        L.flicker_seed = e.flicker_seed;
        L.light_type = e.light_type;
        L.rotation_radians = e.rotation_radians;
        L.spotlight_radius = e.spotlight_radius;
        L.spot_gizmo_scale = e.spot_gizmo_scale;
        L.inner_angle = e.inner_angle;
        L.outer_angle = e.outer_angle;
        L.mlta_active = e.mlta_active;
        L.mlta_amplitude = e.mlta_amplitude;
        L.mlta_frequency = e.mlta_frequency;
        L.mlta_function = e.mlta_function;
        L.mlta_index = e.mlta_index;
        L.texture_index = e.texture_index;
        L.cookie_file_data_id = e.cookie_file_data_id;

        std::uint16_t adt_x = 0, adt_z = 0;
        int mcnk_x = 0, mcnk_z = 0;
        World::worldPosToAdtMcnk(L.position, adt_x, adt_z, mcnk_x, mcnk_z);
        L.tile_x = adt_x;
        L.tile_y = adt_z;

        lights.push_back(L);
      }
    }
  }

  emit pasted();
}

[[nodiscard]]
ChunkCopyFlags Noggit::Ui::Tools::ChunkManipulator::ChunkClipboard::copyParams() const
{
  return _copy_flags;
}

void Noggit::Ui::Tools::ChunkManipulator::ChunkClipboard::setCopyParams(ChunkCopyFlags flags)
{
  _copy_flags = flags;
  // Auto-copy happens when selection paint ends; if the user toggles channels afterwards,
  // rebuild the cache so the preview/paste reflects the new channel set.
  if (_has_last_copy_pos && !_selected_chunks.empty())
  {
    copySelected(_last_copy_pos);
  }
}

[[nodiscard]]
std::set<SelectedChunkIndex> const& Noggit::Ui::Tools::ChunkManipulator::ChunkClipboard::selectedChunks() const
{
  return _selected_chunks;
}

[[nodiscard]]
bool Noggit::Ui::Tools::ChunkManipulator::ChunkClipboard::hasCachedCopy() const
{
  return !_cached_chunks.empty();
}

[[nodiscard]]
std::vector<std::pair<SelectedChunkIndexRelative, ChunkCache>> const&
Noggit::Ui::Tools::ChunkManipulator::ChunkClipboard::cachedChunks() const
{
  return _cached_chunks;
}
