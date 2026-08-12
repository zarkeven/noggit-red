// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class World;

namespace Noggit::MapLights
{
  struct ManifestPointLight
  {
    std::uint32_t id = 0;
    float position_disk[3] {};
    float color[3] { 1.f, 1.f, 1.f };
    float attenuation_start = 0.f;
    float attenuation_end = 10.f;
    float intensity = 1.f;
    std::uint16_t tile_x = 0;
    std::uint16_t tile_y = 0;
    float rotation[3] {};
    std::uint16_t mpl3_flags = 0;
    float mpl3_scale = 0.5f;
    std::uint32_t cookie_file_data_id = 0;
    std::uint8_t flicker_mode = 0;
    float flicker_intensity = 25.f;
    float flicker_speed = 15.f;
    std::uint32_t flicker_seed = 1u;
    bool mlta_active = false;
    float mlta_amplitude = 0.f;
    float mlta_frequency = 0.f;
    int mlta_function = 0;
  };

  struct ManifestSpotLight
  {
    std::uint32_t id = 0;
    float position_disk[3] {};
    float color[3] { 1.f, 1.f, 1.f };
    float attenuation_start = 0.f;
    float attenuation_end = 10.f;
    float intensity = 1.f;
    std::uint16_t tile_x = 0;
    std::uint16_t tile_y = 0;
    float rotation[3] {};
    float spotlight_radius = 15.f;
    float spot_gizmo_scale[3] { 1.f, 1.f, 1.f };
    float inner_angle = 0.5235987755982989f;
    float outer_angle = 0.7853981633974483f;
    std::uint32_t cookie_file_data_id = 0;
    std::uint8_t flicker_mode = 0;
    float flicker_intensity = 25.f;
    float flicker_speed = 15.f;
    std::uint32_t flicker_seed = 1u;
    bool mlta_active = false;
    float mlta_amplitude = 0.f;
    float mlta_frequency = 0.f;
    int mlta_function = 0;
  };

  struct ManifestAdtLightCap
  {
    std::uint16_t tile_x = 0;
    std::uint16_t tile_y = 0;
    std::uint8_t ngpl_cap_encoded = 0;
  };

  struct MapLightsManifest
  {
    int version = 1;
    std::string map;
    std::int32_t lgt_mver = 18;
    std::vector<ManifestPointLight> point_lights;
    std::vector<ManifestSpotLight> spot_lights;
    std::vector<ManifestAdtLightCap> adt_light_caps;
  };

  [[nodiscard]] std::filesystem::path manifest_path_for_map (
    std::filesystem::path const& project_path
  , std::string const& map_basename);

  [[nodiscard]] MapLightsManifest build_from_world (std::string const& map_basename, World const* world);

  [[nodiscard]] bool write_json (MapLightsManifest const& manifest, std::filesystem::path const& path);

  [[nodiscard]] std::optional<MapLightsManifest> read_json (std::filesystem::path const& path);

  [[nodiscard]] bool build_lgt_buffer (MapLightsManifest const& manifest, std::vector<std::uint8_t>& out);

  [[nodiscard]] bool write_lgt_wdt (
    MapLightsManifest const& manifest
  , std::filesystem::path const& path);

  //! Rebuild `_lgt.wdt` bytes from manifest and compare to `written_lgt`.
  [[nodiscard]] bool verify_lgt_round_trip (
    MapLightsManifest const& manifest
  , std::vector<std::uint8_t> const& written_lgt);
}
