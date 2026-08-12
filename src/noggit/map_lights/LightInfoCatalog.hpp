// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/World.h>

#include <QString>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Noggit::MapLights
{
  struct LightAnimPreset
  {
    std::string id;
    std::uint8_t flicker_mode = 0;
    float flicker_intensity = 25.f;
    float flicker_speed = 15.f;
  };

  class LightInfoCatalog
  {
  public:
    static LightInfoCatalog& instance();

    void reload_from_settings();

    [[nodiscard]] std::vector<LightAnimPreset> const& presets() const;
    [[nodiscard]] bool using_fallback_presets() const;
    [[nodiscard]] std::optional<std::filesystem::path> resolved_json_path() const;

    [[nodiscard]] std::optional<std::size_t> match_preset (World::PointLight const& light) const;
    void apply_preset (World::PointLight& light, std::size_t preset_index) const;

    [[nodiscard]] static QString preset_display_label (QString const& preset_id);

  private:
    LightInfoCatalog() = default;

    void load_fallback_presets();
    bool try_load_json (std::filesystem::path const& json_path);

    std::vector<LightAnimPreset> _presets;
    std::optional<std::filesystem::path> _resolved_json_path;
    bool _using_fallback = true;
    std::filesystem::file_time_type _cached_mtime {};
    std::filesystem::path _cached_settings_path;
  };
}
