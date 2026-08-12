// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace Noggit::Integrations
{
  struct EpsilonExportConfig
  {
    std::filesystem::path patches_path;
    std::string patch_name;
    std::string export_map_basename;
    std::uint32_t starting_fdid = 5'000'000;
    //! If non-zero, WDT entry in patch.json uses this FileDataID (Map.db2 / listfile override).
    std::uint32_t wdt_fdid_override = 0;
  };

  class EpsilonPatchExporter
  {
  public:
    static EpsilonPatchExporter& instance();

    static std::optional<EpsilonExportConfig> load_config_from_settings();

    void export_map (EpsilonExportConfig const& cfg
                    , std::filesystem::path const& project_path
                    , std::string const& basename);

  private:
    EpsilonPatchExporter() = default;
  };
}
