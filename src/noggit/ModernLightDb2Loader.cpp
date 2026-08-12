// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/ModernLightDb2Loader.hpp>
#include <noggit/ModernLightTables.hpp>
#include <noggit/Sky.h>
#include <noggit/Log.h>
#include <noggit/MapHeaders.h>
#include <noggit/map_light_target.hpp>
#include <noggit/application/NoggitApplication.hpp>
#include <math/bounding_box.hpp>
#include <math/coordinates.hpp>

#include <ClientData.hpp>
#include <ClientFile.hpp>
#include <Exception.hpp>
#include <Listfile.hpp>

#include <DatabaseDefinition.h>
#include <readers/BlizzardTableReaderFactory.h>
#include <stream/StreamReader.h>
#include <structures/FileStructures.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
  using namespace BlizzardDatabaseLib;

  glm::vec3 color_from_bgr_int(int col)
  {
    return glm::vec3(((col & 0xff0000) >> 16) / 255.f
                   , ((col & 0x00ff00) >> 8) / 255.f
                   , (col & 0x0000ff) / 255.f);
  }

  std::string format_signature_from_magic(unsigned int magic)
  {
    char sig[5];
    sig[0] = static_cast<char>(magic & 0xff);
    sig[1] = static_cast<char>((magic >> 8) & 0xff);
    sig[2] = static_cast<char>((magic >> 16) & 0xff);
    sig[3] = static_cast<char>((magic >> 24) & 0xff);
    sig[4] = '\0';
    return std::string(sig, 4);
  }

  std::string definitions_directory()
  {
    auto* app = Noggit::Application::NoggitApplication::instance();
    if (app)
    {
      if (auto cfg = app->getConfiguration())
      {
        if (!cfg->ApplicationDatabaseDefinitionsPath.empty())
          return cfg->ApplicationDatabaseDefinitionsPath;
      }
    }
    return "definitions";
  }

  std::optional<Structures::VersionDefinition> load_dbd_version(char const* table_name)
  {
    std::filesystem::path const path =
      std::filesystem::path(definitions_directory()) / (std::string(table_name) + ".dbd");

    if (!std::filesystem::exists(path))
    {
      LogDebug << "ModernLightDb2: missing DBD " << path.generic_string() << std::endl;
      return std::nullopt;
    }

    DatabaseDefinition dbd(path.generic_string());
    Structures::VersionDefinition vd;
    Structures::Build const build(Noggit::MapLightTarget::client_build_string);
    if (!dbd.For(build, vd))
    {
      LogDebug << "ModernLightDb2: no DBD layout for " << table_name
               << " at build " << Noggit::MapLightTarget::client_build_string << std::endl;
      return std::nullopt;
    }
    return vd;
  }

  std::shared_ptr<Reader::IBlizzardTableReader> open_db2_table(
    BlizzardArchive::ClientData* client_data
  , char const* table_file
  , Structures::VersionDefinition version_def)
  {
    if (!client_data)
      return nullptr;

    std::string const path = std::string("DBFilesClient\\") + table_file;

    try
    {
      BlizzardArchive::ClientFile file(path, client_data);
      if (file.isEof() || file.getSize() < 8)
        return nullptr;

      char const* buffer = file.getBuffer();
      std::size_t const size = file.getSize();
      unsigned int const magic = *reinterpret_cast<unsigned int const*>(buffer);
      std::string const fmt = format_signature_from_magic(magic);
      // 9.2.7 light tables are WDC3 (wowlib). WDC5 is DF+ and unsupported by our reader factory.
      if (fmt != "WDC3" && fmt != "WDBC")
      {
        LogDebug << "ModernLightDb2: " << table_file << " format " << fmt
                 << " not supported (need WDC3/WDBC for SL 9.2.7)." << std::endl;
        return nullptr;
      }

      auto mem_stream = std::make_shared<Stream::IMemStream>(buffer, size);
      auto stream_reader = std::make_shared<Stream::StreamReader>(mem_stream);

      Reader::BlizzardTableReaderFactory factory;
      auto table = factory.For(stream_reader, version_def, fmt);
      if (!table)
        return nullptr;

      table->LoadTableStructure();
      if (table->RecordCount() == 0)
        return nullptr;

      return table;
    }
    catch (BlizzardArchive::Exceptions::FileReadFailedError const&)
    {
      return nullptr;
    }
    catch (std::exception const&)
    {
      return nullptr;
    }
  }

  int col_int(Structures::BlizzardDatabaseRow const& row, char const* name, int def = 0)
  {
    auto it = row.Columns.find(name);
    if (it == row.Columns.end() || it->second.Value.empty())
      return def;
    try { return std::stoi(it->second.Value); }
    catch (...) { return def; }
  }

  float col_float(Structures::BlizzardDatabaseRow const& row, char const* name, float def = 0.f)
  {
    auto it = row.Columns.find(name);
    if (it == row.Columns.end() || it->second.Value.empty())
      return def;
    try { return std::stof(it->second.Value); }
    catch (...) { return def; }
  }

  std::string col_string(Structures::BlizzardDatabaseRow const& row, char const* name)
  {
    auto it = row.Columns.find(name);
    if (it == row.Columns.end())
      return {};
    return it->second.Value;
  }

  //! DBD arrays use Columns[name].Values; legacy CSV / ColSpec used name_0, name_1, …
  int col_int_at(Structures::BlizzardDatabaseRow const& row, char const* name, std::size_t index, int def = 0)
  {
    auto it = row.Columns.find(name);
    if (it != row.Columns.end() && index < it->second.Values.size())
    {
      try { return std::stoi(it->second.Values[index]); }
      catch (...) { return def; }
    }
    std::string const indexed = std::string(name) + "_" + std::to_string(index);
    return col_int(row, indexed.c_str(), def);
  }

  float col_float_at(Structures::BlizzardDatabaseRow const& row, char const* name, std::size_t index, float def = 0.f)
  {
    auto it = row.Columns.find(name);
    if (it != row.Columns.end() && index < it->second.Values.size())
    {
      try { return std::stof(it->second.Values[index]); }
      catch (...) { return def; }
    }
    std::string const indexed = std::string(name) + "_" + std::to_string(index);
    return col_float(row, indexed.c_str(), def);
  }

  LightDataKeyframe keyframe_from_row(Structures::BlizzardDatabaseRow const& row)
  {
    LightDataKeyframe kf;
    kf.time = col_int(row, "Time");
    kf.colors[LIGHT_GLOBAL_DIFFUSE] = color_from_bgr_int(col_int(row, "DirectColor"));
    kf.colors[LIGHT_GLOBAL_AMBIENT] = color_from_bgr_int(col_int(row, "AmbientColor"));
    kf.colors[SKY_COLOR_TOP] = color_from_bgr_int(col_int(row, "SkyTopColor"));
    kf.colors[SKY_COLOR_MIDDLE] = color_from_bgr_int(col_int(row, "SkyMiddleColor"));
    kf.colors[SKY_COLOR_BAND1] = color_from_bgr_int(col_int(row, "SkyBand1Color"));
    kf.colors[SKY_COLOR_BAND2] = color_from_bgr_int(col_int(row, "SkyBand2Color"));
    kf.colors[SKY_COLOR_SMOG] = color_from_bgr_int(col_int(row, "SkySmogColor"));
    kf.colors[SKY_FOG_COLOR] = color_from_bgr_int(col_int(row, "SkyFogColor"));
    kf.colors[SUN_COLOR] = color_from_bgr_int(col_int(row, "SunColor"));
    kf.colors[SUN_CLOUD_COLOR] = color_from_bgr_int(col_int(row, "CloudSunColor"));
    kf.colors[CLOUD_EMISSIVE_COLOR] = color_from_bgr_int(col_int(row, "CloudEmissiveColor"));
    kf.colors[CLOUD_LAYER1_AMBIENT_COLOR] = color_from_bgr_int(col_int(row, "CloudLayer1AmbientColor"));
    kf.colors[CLOUD_LAYER2_AMBIENT_COLOR] = color_from_bgr_int(col_int(row, "CloudLayer2AmbientColor"));
    kf.colors[OCEAN_COLOR_LIGHT] = color_from_bgr_int(col_int(row, "OceanCloseColor"));
    kf.colors[OCEAN_COLOR_DARK] = color_from_bgr_int(col_int(row, "OceanFarColor"));
    kf.colors[RIVER_COLOR_LIGHT] = color_from_bgr_int(col_int(row, "RiverCloseColor"));
    kf.colors[RIVER_COLOR_DARK] = color_from_bgr_int(col_int(row, "RiverFarColor"));
    kf.colors[SHADOW_OPACITY] = color_from_bgr_int(col_int(row, "ShadowOpacity"));

    kf.fog_end = col_float(row, "FogEnd", 6500.f);
    kf.fog_scaler = col_float(row, "FogScaler", 0.1f);
    kf.fog_density = col_float(row, "FogDensity");
    kf.fog_height = col_float(row, "FogHeight");
    kf.fog_height_scaler = col_float(row, "FogHeightScaler");
    kf.fog_height_density = col_float(row, "FogHeightDensity");
    kf.end_fog_color = color_from_bgr_int(col_int(row, "EndFogColor"));
    kf.end_fog_color_distance = col_float(row, "EndFogColorDistance");
    kf.sun_fog_color = color_from_bgr_int(col_int(row, "SunFogColor"));
    kf.sun_fog_strength = col_float(row, "SunFogStrength", 1.f);
    kf.fog_height_color = color_from_bgr_int(col_int(row, "FogHeightColor"));
    for (int i = 0; i < 4; ++i)
    {
      kf.fog_height_coeff[i] = col_float_at(row, "FogHeightCoefficients", static_cast<std::size_t>(i));
      kf.main_fog_coeff[i] = col_float_at(row, "MainFogCoefficients", static_cast<std::size_t>(i));
    }
    kf.cloud_density = col_float(row, "CloudDensity", 1.f);
    return kf;
  }
}

namespace Noggit::ModernLightDb2
{
  bool try_load_into(ModernLightTables& tables)
  {
    auto* app = Noggit::Application::NoggitApplication::instance();
    if (!app || !app->hasClientData())
      return false;

    BlizzardArchive::ClientData* const cd = app->clientData();

    try
    {
      auto light_def = load_dbd_version("Light");
      auto params_def = load_dbd_version("LightParams");
      auto skybox_def = load_dbd_version("LightSkybox");
      auto light_data_def = load_dbd_version("LightData");
      auto zone_light_def = load_dbd_version("ZoneLight");
      auto zone_point_def = load_dbd_version("ZoneLightPoint");

      if (!light_def || !params_def || !light_data_def)
      {
        LogDebug << "ModernLightDb2: required DBD layouts missing for build "
                 << MapLightTarget::client_build_string << "; will use CSV/DBC fallback."
                 << std::endl;
        return false;
      }

      auto light_table = open_db2_table(cd, "Light.db2", *light_def);
      auto params_table = open_db2_table(cd, "LightParams.db2", *params_def);
      auto skybox_table = skybox_def
        ? open_db2_table(cd, "LightSkybox.db2", *skybox_def)
        : nullptr;
      auto light_data_table = open_db2_table(cd, "LightData.db2", *light_data_def);

      if (!light_table || !params_table || !light_data_table)
      {
        LogDebug << "ModernLightDb2: client DB2 tables missing or unreadable; will use CSV/DBC fallback."
                 << std::endl;
        return false;
      }

      tables.clear_tables();

      if (skybox_table)
      {
        for (std::size_t i = 0; i < skybox_table->RecordCount(); ++i)
        {
          auto row = skybox_table->Record(static_cast<unsigned>(i));
          if (row.RecordId <= 0)
            continue;

          ModernLightSkyboxRecord sb;
          sb.id = row.RecordId;
          sb.name = col_string(row, "Name");
          sb.flags = col_int(row, "Flags");
          sb.file_data_id = col_int(row, "SkyboxFileDataID");
          sb.celestial_file_data_id = col_int(row, "CelestialSkyboxFileDataID");
          tables._skyboxes[static_cast<unsigned>(sb.id)] = std::move(sb);
        }
      }

      for (std::size_t i = 0; i < params_table->RecordCount(); ++i)
      {
        auto row = params_table->Record(static_cast<unsigned>(i));
        if (row.RecordId <= 0)
          continue;

        ModernLightParamRecord rec;
        rec.id = row.RecordId;
        rec.highlight_sky = col_int(row, "HighlightSky") != 0;
        rec.river_shallow_alpha = col_float(row, "WaterShallowAlpha", 0.5f);
        rec.river_deep_alpha = col_float(row, "WaterDeepAlpha", 1.f);
        rec.ocean_shallow_alpha = col_float(row, "OceanShallowAlpha", 0.75f);
        rec.ocean_deep_alpha = col_float(row, "OceanDeepAlpha", 1.f);
        rec.glow = col_float(row, "Glow", 0.5f);
        rec.skybox_id = col_int(row, "LightSkyboxID");
        rec.skybox_flags = col_int(row, "Flags");
        tables._params[static_cast<unsigned>(rec.id)] = std::move(rec);
      }

      for (std::size_t i = 0; i < light_data_table->RecordCount(); ++i)
      {
        auto row = light_data_table->Record(static_cast<unsigned>(i));
        int const param_id = col_int(row, "LightParamID");
        if (param_id <= 0)
          continue;

        auto it = tables._params.find(static_cast<unsigned>(param_id));
        if (it == tables._params.end())
        {
          ModernLightParamRecord rec;
          rec.id = param_id;
          it = tables._params.emplace(static_cast<unsigned>(param_id), std::move(rec)).first;
        }

        it->second.keyframes.push_back(keyframe_from_row(row));
      }

      for (auto& pair : tables._params)
      {
        auto& kfs = pair.second.keyframes;
        std::sort(kfs.begin(), kfs.end(), [](LightDataKeyframe const& a, LightDataKeyframe const& b) {
          return a.time < b.time;
        });
      }

      for (std::size_t i = 0; i < light_table->RecordCount(); ++i)
      {
        auto row = light_table->Record(static_cast<unsigned>(i));
        if (row.RecordId <= 0)
          continue;

        ModernLightRecord rec;
        rec.id = row.RecordId;
        rec.map_id = col_int(row, "ContinentID");
        // Light.db2 GameCoords / GameFalloff are already in yard-scale server/GPS space
        // (unlike classic Light.dbc Position which is stored *36). Dividing by 36 bunched
        // every sky light near the world origin.
        rec.pos = math::to_client(col_float_at(row, "GameCoords", 0)
                                , col_float_at(row, "GameCoords", 1)
                                , col_float_at(row, "GameCoords", 2));
        rec.r1 = col_float(row, "GameFalloffStart");
        rec.r2 = col_float(row, "GameFalloffEnd");

        for (int p = 0; p < kModernSkyParamCount; ++p)
          rec.sky_params[p] = static_cast<unsigned>(col_int_at(row, "LightParamsID", static_cast<std::size_t>(p)));

        tables._lights.push_back(rec);
      }

      if (zone_light_def)
      {
        if (auto zone_table = open_db2_table(cd, "ZoneLight.db2", *zone_light_def))
        {
          for (std::size_t i = 0; i < zone_table->RecordCount(); ++i)
          {
            auto row = zone_table->Record(static_cast<unsigned>(i));
            if (row.RecordId <= 0)
              continue;

            ZoneLight zl;
            zl.id = static_cast<unsigned>(row.RecordId);
            zl.name = col_string(row, "Name");
            zl.lightId = static_cast<unsigned>(col_int(row, "LightID"));
            tables._zone_lights.push_back(zl);
          }
        }
      }

      if (zone_point_def)
      {
        if (auto pt_table = open_db2_table(cd, "ZoneLightPoint.db2", *zone_point_def))
        {
          for (std::size_t i = 0; i < pt_table->RecordCount(); ++i)
          {
            auto row = pt_table->Record(static_cast<unsigned>(i));
            if (row.RecordId <= 0)
              continue;

            ZoneLightPoint pt;
            pt.id = static_cast<unsigned>(row.RecordId);
            pt.zoneLightId = static_cast<unsigned>(col_int(row, "ZoneLightID"));
            pt.posX = -col_float_at(row, "Pos", 1) + ZEROPOINT;
            pt.posY = -col_float_at(row, "Pos", 0) + ZEROPOINT;
            pt.pointOrder = static_cast<unsigned>(col_int(row, "PointOrder"));
            tables._zone_light_points[pt.zoneLightId].push_back(pt);
          }

          for (auto& zl : tables._zone_lights)
          {
            auto it = tables._zone_light_points.find(zl.id);
            if (it == tables._zone_light_points.end())
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
      }

      if (tables._lights.empty() || tables._params.empty())
      {
        tables.clear_tables();
        return false;
      }

      LogDebug << "ModernLightDb2: loaded " << tables._lights.size() << " lights, "
               << tables._params.size() << " params from client DB2 (build "
               << MapLightTarget::client_build_string << ")." << std::endl;
      tables._loaded_from_db2 = true;
      return true;
    }
    catch (std::exception const& e)
    {
      LogError << "ModernLightDb2: " << e.what() << " — using CSV/DBC fallback." << std::endl;
      tables.clear_tables();
      return false;
    }
    catch (...)
    {
      LogError << "ModernLightDb2: unknown error while reading client DB2 — using CSV/DBC fallback."
               << std::endl;
      tables.clear_tables();
      return false;
    }
  }
}
