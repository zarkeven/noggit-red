// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#ifndef NOGGIT_CHUNKCLIPBOARD_HPP
#define NOGGIT_CHUNKCLIPBOARD_HPP

#include <noggit/MapHeaders.h>
#include <noggit/texture_set.hpp>
#include <noggit/TileIndex.hpp>
#include <noggit/World.h>

#include <blizzard-archive-library/include/Listfile.hpp>

#include <QObject>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

class Alphamap;
class liquid_layer;
struct tmp_edit_alpha_values;

namespace Noggit
{
  class Action;
}

namespace Noggit::Ui::Tools::ChunkManipulator
{
  enum class ChunkCopyFlags : unsigned
  {
    TERRAIN = 0x1,
    LIQUID = 0x2,
    WMOs = 0x4,
    MODELS = 0x8,
    SHADOWS = 0x10,
    TEXTURES = 0x20,
    VERTEX_COLORS = 0x40,
    HOLES = 0x80,
    FLAGS = 0x100,
    AREA_ID = 0x200,
    POINT_LIGHTS = 0x400
  };

  [[nodiscard]] inline constexpr unsigned to_underlying(ChunkCopyFlags f) noexcept
  {
    return static_cast<unsigned>(f);
  }

  [[nodiscard]] inline constexpr ChunkCopyFlags chunk_copy_flags_all() noexcept
  {
    return static_cast<ChunkCopyFlags>(
        static_cast<unsigned>(ChunkCopyFlags::TERRAIN)
      | static_cast<unsigned>(ChunkCopyFlags::LIQUID)
      | static_cast<unsigned>(ChunkCopyFlags::WMOs)
      | static_cast<unsigned>(ChunkCopyFlags::MODELS)
      | static_cast<unsigned>(ChunkCopyFlags::SHADOWS)
      | static_cast<unsigned>(ChunkCopyFlags::TEXTURES)
      | static_cast<unsigned>(ChunkCopyFlags::VERTEX_COLORS)
      | static_cast<unsigned>(ChunkCopyFlags::HOLES)
      | static_cast<unsigned>(ChunkCopyFlags::FLAGS)
      | static_cast<unsigned>(ChunkCopyFlags::AREA_ID)
      | static_cast<unsigned>(ChunkCopyFlags::POINT_LIGHTS));
  }

  [[nodiscard]] inline constexpr bool chunk_copy_flags_test(ChunkCopyFlags set, ChunkCopyFlags bit) noexcept
  {
    return (to_underlying(set) & to_underlying(bit)) != 0;
  }

  enum class ChunkPasteFlags
  {
    REPLACE = 0x1
  };

  enum class ChunkSelectionMode
  {
    SELECT,
    DESELECT
  };

  struct ChunkTextureCache
  {
    size_t n_textures;
    std::vector<std::string> textures;
    std::array<std::unique_ptr<Alphamap>, 3> alphamaps;
    std::unique_ptr<tmp_edit_alpha_values> tmp_edit_values;
    layer_info layers_info[4]{};
  };

  enum class ChunkManipulatorObjectTypes
  {
    WMO,
    M2
  };

  struct ChunkObjectCacheEntry
  {
    BlizzardArchive::Listfile::FileKey file_key;
    ChunkManipulatorObjectTypes type;
    glm::vec3 pos;
    glm::vec3 dir;
    float scale;
  };

  struct ChunkPointLightCacheEntry
  {
    glm::vec3 rel_position{};
    glm::vec3 color{1.f, 1.f, 1.f};
    float attenuation_start = 0.f;
    float attenuation_end = 0.f;
    float intensity = 1.f;
    std::uint8_t flicker_mode = 0;
    float flicker_intensity = 25.f;
    float flicker_speed = 15.f;
    std::uint32_t flicker_seed = 1u;
    World::MapLightType light_type = World::MapLightType::Point;
    glm::vec3 rotation_radians{};
    float spotlight_radius = 15.f;
    glm::vec3 spot_gizmo_scale { 1.f, 1.f, 1.f };
    float inner_angle = 0.5235987755982989f;
    float outer_angle = 0.7853981633974483f;
    bool mlta_active = false;
    float mlta_amplitude = 0.f;
    float mlta_frequency = 0.f;
    int mlta_function = 0;
    std::int16_t mlta_index = -1;
    std::int16_t texture_index = -1;
    std::uint32_t cookie_file_data_id = 0;
  };


  struct SelectedChunkIndex
  {
    TileIndex tile_index;
    unsigned x;
    unsigned z;

    SelectedChunkIndex() = delete;
    SelectedChunkIndex(TileIndex ti, unsigned cx, unsigned cz)
      : tile_index(ti)
      , x(cx)
      , z(cz)
    {
    }

    friend bool operator<(SelectedChunkIndex const& lhs, SelectedChunkIndex const& rhs)
    {
      return std::tie(lhs.tile_index, lhs.x, lhs.z) < std::tie(rhs.tile_index, rhs.x, rhs.z);
    }
  };

  struct SelectedChunkIndexRelative : public SelectedChunkIndex
  {
    int rel_x;
    int rel_z;

    SelectedChunkIndexRelative(SelectedChunkIndex const& idx, int rx, int rz)
      : SelectedChunkIndex(idx)
      , rel_x(rx)
      , rel_z(rz)
    {
    }

    friend bool operator<(SelectedChunkIndexRelative const& lhs, SelectedChunkIndexRelative const& rhs)
    {
      return std::tie(lhs.tile_index, lhs.x, lhs.z, lhs.rel_x, lhs.rel_z)
        < std::tie(rhs.tile_index, rhs.x, rhs.z, rhs.rel_x, rhs.rel_z);
    }

  };

  struct ChunkLiquidLayerCacheEntry
  {
    int liquid_id = 0;
    std::uint64_t subchunks = 0;
    // Per-vertex: (height, depth, u, v) for 9*9 vertices.
    std::array<float, 9 * 9 * 4> vertex_data{};
  };

  struct ChunkCache
  {
    std::optional<std::array<char, 145 * 3 * sizeof(float)>> terrain_height;
    std::optional<std::array<char, 145 * 3 * sizeof(float)>> vertex_colors;
    std::optional<std::array<std::uint8_t, 64 * 64>> shadows;
    std::optional<std::vector<ChunkLiquidLayerCacheEntry>> liquid_layers;
    std::optional<ChunkTextureCache> textures;
    std::optional<std::vector<ChunkObjectCacheEntry>> objects;
    std::optional<int> holes;
    std::optional<mcnk_flags> flags;
    std::optional<int> area_id;
    std::optional<std::vector<ChunkPointLightCacheEntry>> point_lights;
  };

  class ChunkClipboard : public QObject
  {
    Q_OBJECT
  public:
    explicit ChunkClipboard(World* world, QObject* parent = nullptr);

    void selectRange(glm::vec3 const& cursor_pos, float radius, ChunkSelectionMode mode);
    void selectChunk(glm::vec3 const& pos, ChunkSelectionMode mode);
    void selectChunk(TileIndex const& tile_index, unsigned x, unsigned z, ChunkSelectionMode mode);
    /// Copies the current quick selection into the internal cache using `copyParams()`
    /// (set from the Chunk Manipulator “what to copy” UI).
    void copySelected(glm::vec3 const& pos);
    void clearSelection();
    /// When \a undo_action is non-null, registers pre-state on each mutated chunk (caller must `beginAction` / `endAction`).
    void pasteSelection(glm::vec3 const& pos, ChunkPasteFlags flags, Noggit::Action* undo_action = nullptr);

    [[nodiscard]]
    ChunkCopyFlags copyParams() const;
    void setCopyParams(ChunkCopyFlags flags);

    [[nodiscard]]
    std::set<SelectedChunkIndex> const& selectedChunks() const;

    [[nodiscard]]
    bool hasCachedCopy() const;

    [[nodiscard]]
    std::vector<std::pair<SelectedChunkIndexRelative, ChunkCache>> const& cachedChunks() const;

    /// Added to pasted terrain vertex Y, liquid surface heights, and pasted point-light Y after paste (preview uses this too).
    [[nodiscard]]
    float pasteTerrainHeightOffset() const;
    void setPasteTerrainHeightOffset(float y);

  signals:
    void selectionChanged(std::set<SelectedChunkIndex> const& selected_chunks);
    void selectionCleared();
    void pasted();

  private:
    std::set<SelectedChunkIndex> _selected_chunks;
    std::vector<std::pair<SelectedChunkIndexRelative, ChunkCache>> _cached_chunks;
    World* _world;
    ChunkCopyFlags _copy_flags;

    glm::vec3 _last_copy_pos{};
    bool _has_last_copy_pos = false;

    float _paste_terrain_height_offset = 0.f;
  };
}

#endif //NOGGIT_CHUNKCLIPBOARD_HPP
