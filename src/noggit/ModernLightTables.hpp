// This file is part of Noggit3, licensed under GNU General Public License (version 3).
#pragma once

#include <noggit/ModernLightData.hpp>
#include <noggit/ContextObject.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct ZoneLight;
struct ZoneLightPoint;
class SkyParam;

class ModernLightTables;

namespace Noggit::ModernLightDb2
{
  bool try_load_into(ModernLightTables& tables);
}

[[nodiscard]] bool noggit_modern_features_enabled();
[[nodiscard]] bool noggit_use_modern_sky_lights();

class ModernLightTables
{
  friend bool Noggit::ModernLightDb2::try_load_into(ModernLightTables& tables);

public:
  static ModernLightTables& instance();

  void ensure_loaded();
  void invalidate();
  void clear_tables();

  [[nodiscard]] std::vector<ModernLightRecord> lights_for_map(unsigned map_id) const;
  [[nodiscard]] ModernLightParamRecord const* param(unsigned param_id) const;
  [[nodiscard]] ModernLightRecord const* find_light(int light_id) const;
  [[nodiscard]] bool has_usable_data() const;
  void fill_zone_lights(unsigned map_id, std::vector<ZoneLight>& out) const;

  void init_sky_param(unsigned param_id, SkyParam& param, Noggit::NoggitRenderContext context) const;

private:
  ModernLightTables() = default;

  void load_from_csv();
  [[nodiscard]] std::string resolve_csv_path(char const* filename) const;

  bool _loaded = false;
  bool _loaded_from_db2 = false;
  std::vector<ModernLightRecord> _lights;
  std::unordered_map<unsigned, ModernLightParamRecord> _params;
  std::unordered_map<unsigned, ModernLightSkyboxRecord> _skyboxes;
  std::vector<ZoneLight> _zone_lights;
  std::unordered_map<unsigned, std::vector<ZoneLightPoint>> _zone_light_points;
};
