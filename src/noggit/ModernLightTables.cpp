// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/ModernLightTables.hpp>
#include <noggit/ModernLightDb2Loader.hpp>
#include <noggit/DBC.h>
#include <noggit/Log.h>
#include <noggit/MapHeaders.h>
#include <noggit/Sky.h>

#include <math/bounding_box.hpp>
#include <math/coordinates.hpp>
#include <noggit/ModelManager.h>
#include <noggit/application/NoggitApplication.hpp>
#include <noggit/application/Configuration/NoggitApplicationConfiguration.hpp>
#include <noggit/project/CurrentProject.hpp>

#include <Listfile.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace
{
  glm::vec3 color_from_bgr_int(int col)
  {
    return glm::vec3(((col & 0xff0000) >> 16) / 255.f
                   , ((col & 0x00ff00) >> 8) / 255.f
                   , (col & 0x0000ff) / 255.f);
  }

  int field_index(std::vector<QString> const& header, char const* name)
  {
    QString const key = QString::fromUtf8(name);
    for (int i = 0; i < header.size(); ++i)
    {
      if (header[i].compare(key, Qt::CaseInsensitive) == 0)
        return i;
    }
    return -1;
  }

  float field_float(QStringList const& fields, int idx, float def = 0.f)
  {
    if (idx < 0 || idx >= fields.size())
      return def;
    bool ok = false;
    float v = fields[idx].toFloat(&ok);
    return ok ? v : def;
  }

  int field_int(QStringList const& fields, int idx, int def = 0)
  {
    if (idx < 0 || idx >= fields.size())
      return def;
    bool ok = false;
    int v = fields[idx].toInt(&ok);
    return ok ? v : def;
  }
}

bool noggit_modern_features_enabled()
{
  auto* app = Noggit::Application::NoggitApplication::instance();
  return app && app->getConfiguration()->modern_features;
}

bool noggit_use_modern_sky_lights()
{
  if (!noggit_modern_features_enabled())
    return false;

  try
  {
    if (Noggit::Project::CurrentProject::get()->projectVersion
        == Noggit::Project::ProjectVersion::WOTLK)
    {
      return false;
    }
  }
  catch (...)
  {
  }

  ModernLightTables::instance().ensure_loaded();
  return ModernLightTables::instance().has_usable_data();
}

ModernLightTables& ModernLightTables::instance()
{
  static ModernLightTables tables;
  return tables;
}

void ModernLightTables::invalidate()
{
  _loaded = false;
  _loaded_from_db2 = false;
  _lights.clear();
  _params.clear();
  _skyboxes.clear();
  _zone_lights.clear();
  _zone_light_points.clear();
}

void ModernLightTables::clear_tables()
{
  _lights.clear();
  _params.clear();
  _skyboxes.clear();
  _zone_lights.clear();
  _zone_light_points.clear();
  _loaded = false;
  _loaded_from_db2 = false;
}

void ModernLightTables::ensure_loaded()
{
  if (_loaded)
    return;

  if (noggit_modern_features_enabled())
  {
    if (Noggit::ModernLightDb2::try_load_into(*this))
    {
      _loaded = true;
      return;
    }

    load_from_csv();
    if (has_usable_data())
    {
      LogDebug << "ModernLightTables: using bundled CSV light definitions." << std::endl;
      _loaded = true;
      return;
    }

    LogDebug << "ModernLightTables: no client DB2 or CSV light data; Skies will use project DBC."
             << std::endl;
    _loaded = true;
    return;
  }

  load_from_csv();
  _loaded = true;
}

std::string ModernLightTables::resolve_csv_path(char const* filename) const
{
  auto* app = Noggit::Application::NoggitApplication::instance();
  std::string const base = app
    ? app->getConfiguration()->ApplicationNoggitDefinitionsPath
    : std::string("noggit-definitions");

  std::string const primary = base + "/db2/" + filename;
  if (QFile::exists(QString::fromStdString(primary)))
    return primary;

  QDir const exe_dir(QCoreApplication::applicationDirPath());
  std::string const bundled = exe_dir.filePath("noggit-definitions/db2/" + QString::fromUtf8(filename)).toStdString();
  if (QFile::exists(QString::fromStdString(bundled)))
    return bundled;

  return primary;
}

void ModernLightTables::load_from_csv()
{
  auto load_table = [&](char const* name, auto const& fn) {
    std::string const path = resolve_csv_path(name);
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
      LogError << "ModernLightTables: could not open " << path << std::endl;
      return;
    }
    QTextStream in(&file);
    QString header_line = in.readLine();
    std::vector<QString> header;
    for (QString const& h : header_line.split(','))
      header.push_back(h.trimmed());
    while (!in.atEnd())
    {
      QString line = in.readLine().trimmed();
      if (line.isEmpty())
        continue;
      fn(header, line.split(','));
    }
  };

  load_table("LightParams.csv", [&](std::vector<QString> const& header, QStringList const& fields) {
    int const id = field_int(fields, field_index(header, "ID"));
    if (id <= 0)
      return;
    ModernLightParamRecord rec;
    rec.id = id;
    rec.highlight_sky = field_int(fields, field_index(header, "HighlightSky")) != 0;
    rec.river_shallow_alpha = field_float(fields, field_index(header, "WaterShallowAlpha"), 0.5f);
    rec.river_deep_alpha = field_float(fields, field_index(header, "WaterDeepAlpha"), 1.f);
    rec.ocean_shallow_alpha = field_float(fields, field_index(header, "OceanShallowAlpha"), 0.75f);
    rec.ocean_deep_alpha = field_float(fields, field_index(header, "OceanDeepAlpha"), 1.f);
    rec.glow = field_float(fields, field_index(header, "Glow"), 0.5f);
    rec.skybox_id = field_int(fields, field_index(header, "LightSkyboxID"));
    rec.skybox_flags = field_int(fields, field_index(header, "Flags"));
    _params[static_cast<unsigned>(id)] = std::move(rec);
  });

  load_table("LightSkybox.csv", [&](std::vector<QString> const& header, QStringList const& fields) {
    int const id = field_int(fields, field_index(header, "ID"));
    if (id <= 0)
      return;
    ModernLightSkyboxRecord rec;
    rec.id = id;
    int const name_idx = field_index(header, "Name");
    rec.name = (name_idx >= 0 && name_idx < fields.size())
      ? fields[name_idx].trimmed().toStdString()
      : std::string();
    rec.flags = field_int(fields, field_index(header, "Flags"));
    rec.file_data_id = field_int(fields, field_index(header, "SkyboxFileDataID"));
    rec.celestial_file_data_id = field_int(fields, field_index(header, "CelestialSkyboxFileDataID"));
    _skyboxes[static_cast<unsigned>(id)] = std::move(rec);
  });

  load_table("LightData.csv", [&](std::vector<QString> const& header, QStringList const& fields) {
    int const param_id = field_int(fields, field_index(header, "LightParamID"));
    if (param_id <= 0)
      return;

    auto it = _params.find(static_cast<unsigned>(param_id));
    if (it == _params.end())
    {
      ModernLightParamRecord rec;
      rec.id = param_id;
      it = _params.emplace(static_cast<unsigned>(param_id), std::move(rec)).first;
    }

    LightDataKeyframe kf;
    kf.time = field_int(fields, field_index(header, "Time"));
    kf.colors[LIGHT_GLOBAL_DIFFUSE] = color_from_bgr_int(field_int(fields, field_index(header, "DirectColor")));
    kf.colors[LIGHT_GLOBAL_AMBIENT] = color_from_bgr_int(field_int(fields, field_index(header, "AmbientColor")));
    kf.colors[SKY_COLOR_TOP] = color_from_bgr_int(field_int(fields, field_index(header, "SkyTopColor")));
    kf.colors[SKY_COLOR_MIDDLE] = color_from_bgr_int(field_int(fields, field_index(header, "SkyMiddleColor")));
    kf.colors[SKY_COLOR_BAND1] = color_from_bgr_int(field_int(fields, field_index(header, "SkyBand1Color")));
    kf.colors[SKY_COLOR_BAND2] = color_from_bgr_int(field_int(fields, field_index(header, "SkyBand2Color")));
    kf.colors[SKY_COLOR_SMOG] = color_from_bgr_int(field_int(fields, field_index(header, "SkySmogColor")));
    kf.colors[SKY_FOG_COLOR] = color_from_bgr_int(field_int(fields, field_index(header, "SkyFogColor")));
    kf.colors[SUN_COLOR] = color_from_bgr_int(field_int(fields, field_index(header, "SunColor")));
    kf.colors[SUN_CLOUD_COLOR] = color_from_bgr_int(field_int(fields, field_index(header, "CloudSunColor")));
    kf.colors[CLOUD_EMISSIVE_COLOR] = color_from_bgr_int(field_int(fields, field_index(header, "CloudEmissiveColor")));
    kf.colors[CLOUD_LAYER1_AMBIENT_COLOR] = color_from_bgr_int(field_int(fields, field_index(header, "CloudLayer1AmbientColor")));
    kf.colors[CLOUD_LAYER2_AMBIENT_COLOR] = color_from_bgr_int(field_int(fields, field_index(header, "CloudLayer2AmbientColor")));
    kf.colors[OCEAN_COLOR_LIGHT] = color_from_bgr_int(field_int(fields, field_index(header, "OceanCloseColor")));
    kf.colors[OCEAN_COLOR_DARK] = color_from_bgr_int(field_int(fields, field_index(header, "OceanFarColor")));
    kf.colors[RIVER_COLOR_LIGHT] = color_from_bgr_int(field_int(fields, field_index(header, "RiverCloseColor")));
    kf.colors[RIVER_COLOR_DARK] = color_from_bgr_int(field_int(fields, field_index(header, "RiverFarColor")));
    kf.colors[SHADOW_OPACITY] = color_from_bgr_int(field_int(fields, field_index(header, "ShadowOpacity")));

    kf.fog_end = field_float(fields, field_index(header, "FogEnd"), 6500.f);
    kf.fog_scaler = field_float(fields, field_index(header, "FogScaler"), 0.1f);
    kf.fog_density = field_float(fields, field_index(header, "FogDensity"));
    kf.fog_height = field_float(fields, field_index(header, "FogHeight"));
    kf.fog_height_scaler = field_float(fields, field_index(header, "FogHeightScaler"));
    kf.fog_height_density = field_float(fields, field_index(header, "FogHeightDensity"));
    kf.end_fog_color = color_from_bgr_int(field_int(fields, field_index(header, "EndFogColor")));
    kf.end_fog_color_distance = field_float(fields, field_index(header, "EndFogColorDistance"));
    kf.sun_fog_color = color_from_bgr_int(field_int(fields, field_index(header, "SunFogColor")));
    kf.sun_fog_strength = field_float(fields, field_index(header, "SunFogStrength"), 1.f);
    kf.fog_height_color = color_from_bgr_int(field_int(fields, field_index(header, "FogHeightColor")));
    kf.fog_height_coeff[0] = field_float(fields, field_index(header, "FogHeightCoefficients_0"));
    kf.fog_height_coeff[1] = field_float(fields, field_index(header, "FogHeightCoefficients_1"));
    kf.fog_height_coeff[2] = field_float(fields, field_index(header, "FogHeightCoefficients_2"));
    kf.fog_height_coeff[3] = field_float(fields, field_index(header, "FogHeightCoefficients_3"));
    kf.main_fog_coeff[0] = field_float(fields, field_index(header, "MainFogCoefficients_0"));
    kf.main_fog_coeff[1] = field_float(fields, field_index(header, "MainFogCoefficients_1"));
    kf.main_fog_coeff[2] = field_float(fields, field_index(header, "MainFogCoefficients_2"));
    kf.main_fog_coeff[3] = field_float(fields, field_index(header, "MainFogCoefficients_3"));
    kf.cloud_density = field_float(fields, field_index(header, "CloudDensity"), 1.f);

    it->second.keyframes.push_back(kf);
  });

  for (auto& pair : _params)
  {
    auto& kfs = pair.second.keyframes;
    std::sort(kfs.begin(), kfs.end(), [](LightDataKeyframe const& a, LightDataKeyframe const& b) {
      return a.time < b.time;
    });
  }

  load_table("Light.csv", [&](std::vector<QString> const& header, QStringList const& fields) {
    ModernLightRecord rec;
    rec.id = field_int(fields, field_index(header, "ID"));
    rec.map_id = field_int(fields, field_index(header, "ContinentID"));
    if (rec.id <= 0)
      return;

    // Light.csv GameCoords are yard-scale server/GPS (not classic Light.dbc *36).
    rec.pos = math::to_client(field_float(fields, field_index(header, "GameCoords_0"))
                            , field_float(fields, field_index(header, "GameCoords_1"))
                            , field_float(fields, field_index(header, "GameCoords_2")));
    rec.r1 = field_float(fields, field_index(header, "GameFalloffStart"));
    rec.r2 = field_float(fields, field_index(header, "GameFalloffEnd"));

    for (int i = 0; i < NUM_SkyParamsNames; ++i)
    {
      std::string col = "LightParamsID_" + std::to_string(i);
      rec.sky_params[i] = static_cast<unsigned>(field_int(fields, field_index(header, col.c_str())));
    }
    _lights.push_back(rec);
  });

  load_table("ZoneLight.csv", [&](std::vector<QString> const& header, QStringList const& fields) {
    ZoneLight zl;
    zl.id = static_cast<unsigned>(field_int(fields, field_index(header, "ID")));
    zl.name = field_index(header, "Name") >= 0 && field_index(header, "Name") < fields.size()
      ? fields[field_index(header, "Name")].toStdString()
      : std::string();
    int const map_id = field_int(fields, field_index(header, "MapID"));
    zl.lightId = static_cast<unsigned>(field_int(fields, field_index(header, "LightID")));
    if (zl.id == 0)
      return;
    (void)map_id;
    _zone_lights.push_back(zl);
  });

  load_table("ZoneLightPoint.csv", [&](std::vector<QString> const& header, QStringList const& fields) {
    ZoneLightPoint pt;
    pt.id = static_cast<unsigned>(field_int(fields, field_index(header, "ID")));
    int const zl_id = field_index(header, "ZoneLightID") >= 0
      ? field_int(fields, field_index(header, "ZoneLightID"))
      : field_int(fields, field_index(header, "iRefID_ZoneLight"));
    pt.zoneLightId = static_cast<unsigned>(zl_id);
    float const p0 = field_float(fields, field_index(header, "Pos_0"));
    float const p1 = field_float(fields, field_index(header, "Pos_1"));
    if (field_index(header, "Position_0") >= 0)
    {
      pt.posX = -field_float(fields, field_index(header, "Position_1")) + ZEROPOINT;
      pt.posY = -field_float(fields, field_index(header, "Position_0")) + ZEROPOINT;
    }
    else
    {
      pt.posX = -p1 + ZEROPOINT;
      pt.posY = -p0 + ZEROPOINT;
    }
    pt.pointOrder = static_cast<unsigned>(field_int(fields, field_index(header, "PointOrder")));
    _zone_light_points[pt.zoneLightId].push_back(pt);
  });

  for (auto& zl : _zone_lights)
  {
    auto it = _zone_light_points.find(zl.id);
    if (it == _zone_light_points.end())
      continue;

    auto& list = it->second;
    std::sort(list.begin(), list.end(), [](ZoneLightPoint const& a, ZoneLightPoint const& b) {
      return a.pointOrder < b.pointOrder;
    });

    for (auto const& pt : list)
      zl.points.push_back(glm::vec2(pt.posX, pt.posY));

    if (zl.points.size() >= 3)
    {
      math::aabb_2d const bounds(zl.points);
      zl._extents[0] = bounds.min;
      zl._extents[1] = bounds.max;
    }
  }
}

bool ModernLightTables::has_usable_data() const
{
  return !_lights.empty() || !_params.empty();
}

ModernLightRecord const* ModernLightTables::find_light(int light_id) const
{
  for (auto const& rec : _lights)
  {
    if (rec.id == light_id)
      return &rec;
  }
  return nullptr;
}

std::vector<ModernLightRecord> ModernLightTables::lights_for_map(unsigned map_id) const
{
  std::vector<ModernLightRecord> out;
  for (auto const& rec : _lights)
  {
    if (static_cast<unsigned>(rec.map_id) == map_id)
      out.push_back(rec);
  }
  return out;
}

ModernLightParamRecord const* ModernLightTables::param(unsigned param_id) const
{
  auto it = _params.find(param_id);
  return it == _params.end() ? nullptr : &it->second;
}

void ModernLightTables::fill_zone_lights(unsigned map_id, std::vector<ZoneLight>& out) const
{
  for (auto const& zl : _zone_lights)
  {
    (void)map_id;
    if (zl.points.size() >= 3)
      out.push_back(zl);
  }
}

void ModernLightTables::init_sky_param(unsigned param_id, SkyParam& param, Noggit::NoggitRenderContext context) const
{
  ModernLightParamRecord const* rec = this->param(param_id);
  if (!rec)
    return;

  param.Id = param_id;
  param.set_highlight_sky(rec->highlight_sky);
  param.set_river_shallow_alpha(rec->river_shallow_alpha);
  param.set_river_deep_alpha(rec->river_deep_alpha);
  param.set_ocean_shallow_alpha(rec->ocean_shallow_alpha);
  param.set_ocean_deep_alpha(rec->ocean_deep_alpha);
  param.set_glow(rec->glow);

  if (rec->skybox_id > 0 && !param.skybox.has_value())
  {
    auto const try_load_model = [&](int file_data_id, std::string const& path) -> std::optional<ModelInstance>
    {
      try
      {
        if (file_data_id > 0)
        {
          auto* app = Noggit::Application::NoggitApplication::instance();
          auto* listfile = app && app->hasClientData()
            ? const_cast<BlizzardArchive::Listfile::Listfile*>(app->clientData()->listfile())
            : nullptr;
          BlizzardArchive::Listfile::FileKey const key(
            static_cast<std::uint32_t>(file_data_id), listfile);
          return ModelInstance(key, context);
        }
        if (!path.empty())
          return ModelInstance(BlizzardArchive::Listfile::FileKey(path), context);
      }
      catch (...)
      {
      }
      return std::nullopt;
    };

    auto const sb_it = _skyboxes.find(static_cast<unsigned>(rec->skybox_id));
    if (sb_it != _skyboxes.end())
    {
      ModernLightSkyboxRecord const& sb = sb_it->second;
      int const flags = rec->skybox_flags != 0 ? rec->skybox_flags : sb.flags;
      if (auto main = try_load_model(sb.file_data_id, sb.name))
      {
        param.skybox = std::move(*main);
        param.skyboxFlags = flags;
      }
      if (sb.celestial_file_data_id > 0)
      {
        if (auto cel = try_load_model(sb.celestial_file_data_id, {}))
          param.celestial_skybox = std::move(*cel);
      }
    }
    else
    {
      try
      {
        auto skyboxRec = gLightSkyboxDB.getByID(rec->skybox_id);
        if (auto main = try_load_model(0, skyboxRec.getString(LightSkyboxDB::filename)))
        {
          param.skybox = std::move(*main);
          param.skyboxFlags = skyboxRec.getInt(LightSkyboxDB::flags);
        }
      }
      catch (...)
      {
        (void)rec->skybox_id;
      }
    }
  }

  param.light_data_keyframes = rec->keyframes;
  param.uses_modern_light_data = !rec->keyframes.empty();
}
