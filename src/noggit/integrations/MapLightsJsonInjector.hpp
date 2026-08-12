// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/integrations/EpsilonPatchExporter.hpp>
#include <noggit/map_lights/MapLightsManifest.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace Noggit::Integrations
{
  class MapLightsJsonInjector
  {
  public:
    struct Result
    {
      bool success = false;
      std::string message;
    };

    //! Read `{map}_lights.json` from the project and inject `_lgt.wdt`, WDT `lgtFileDataID`, and NGPL into the Epsilon patch.
    static Result inject (
      EpsilonExportConfig const& cfg
    , std::filesystem::path const& project_path
    , std::string const& map_basename);

    //! Inject only NGPL caps from an already-loaded manifest (used after Epsilon export wrote WDT/_lgt).
    static Result inject_ngpl_caps (
      MapLights::MapLightsManifest const& manifest
    , EpsilonExportConfig const& cfg
    , std::string const& map_basename);
  };
}
