// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/map_lights/MapLightsManifest.hpp>
#include <noggit/World.h>
#include <noggit/MapTile.h>
#include <noggit/map_index.hpp>
#include <noggit/map_light_target.hpp>
#include <noggit/format/ChunkReader.hpp>
#include <noggit/Misc.h>
#include <noggit/Log.h>
#include <noggit/project/CurrentProject.hpp>
#include <noggit/TileIndex.hpp>
#include <util/sExtendableArray.hpp>

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

namespace
{
#pragma pack(push, 1)
  struct MPL3Record
  {
    std::uint32_t light_index;
    std::uint8_t color_bgra[4];
    float position[3];
    float attenuation_start;
    float attenuation_end;
    float intensity;
    float rotation[3];
    std::uint16_t tile_x;
    std::uint16_t tile_y;
    std::int16_t mlta_index;
    std::int16_t texture_index;
    std::uint16_t flags;
    std::uint16_t scale_half;
  };
  static_assert(sizeof(MPL3Record) == 0x38, "MPL3 record must be 0x38 bytes");

  struct MSLTRecord
  {
    std::uint32_t id;
    std::uint8_t color_bgra[4];
    float position[3];
    float attenuation_start;
    float attenuation_end;
    float intensity;
    float rotation[3];
    float spotlight_radius;
    float inner_angle;
    float outer_angle;
    std::uint16_t tile_x;
    std::uint16_t tile_y;
    std::int16_t mlta_index;
    std::int16_t texture_index;
  };
  static_assert(sizeof(MSLTRecord) == 0x40, "MSLT record must be 0x40 bytes");
#pragma pack(pop)

  struct MltaRow
  {
    float amplitude;
    float frequency;
    int function;
  };

  std::uint16_t float_to_half_bits (float value)
  {
    std::uint32_t x;
    std::memcpy (&x, &value, sizeof (float));
    std::uint32_t const sign = (x >> 16) & 0x8000u;
    int exp = int ((x >> 23) & 0xff) - 127 + 15;
    std::uint32_t mant = x & 0x007fffffu;
    if (exp <= 0)
      return static_cast<std::uint16_t>(sign);
    if (exp >= 31)
      return static_cast<std::uint16_t>(sign | 0x7c00u);
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exp) << 10) | (mant >> 13));
  }

  void clamp_attenuation (float& a_start, float& a_end)
  {
    if (!std::isfinite(a_start))
      a_start = 0.f;
    if (!std::isfinite(a_end))
      a_end = 0.f;
    if (a_start < 0.f)
      a_start = 0.f;
    if (a_end < 0.f)
      a_end = 0.f;
    if (a_start > a_end)
      std::swap(a_start, a_end);
  }

  struct FlickerFields
  {
    std::uint8_t flicker_mode = 0;
    float flicker_intensity = 25.f;
    float flicker_speed = 15.f;
    std::uint32_t flicker_seed = 1u;
    bool mlta_active = false;
    float mlta_amplitude = 0.f;
    float mlta_frequency = 0.f;
    int mlta_function = 0;
  };

  std::int16_t alloc_mlta (FlickerFields const& light, std::vector<MltaRow>& mlta_out)
  {
    int fn = 0;
    float a = 0.f;
    float f = 0.f;
    if (light.mlta_active && light.mlta_function > 0)
    {
      fn = light.mlta_function;
      a = light.mlta_amplitude;
      f = light.mlta_frequency;
    }
    else if (light.flicker_mode != 0)
    {
      fn = light.flicker_mode;
      a = light.flicker_intensity;
      f = light.flicker_speed;
    }
    else
      return static_cast<std::int16_t>(-1);

    for (std::size_t j = 0; j < mlta_out.size(); ++j)
    {
      if (mlta_out[j].function == fn
          && std::abs (mlta_out[j].amplitude - a) < 1e-3f
          && std::abs (mlta_out[j].frequency - f) < 1e-3f)
        return static_cast<std::int16_t>(j);
    }
    mlta_out.push_back ({ a, f, fn });
    return static_cast<std::int16_t>(mlta_out.size() - 1);
  }

  QJsonArray vec3_to_json (float const v[3])
  {
    QJsonArray arr;
    arr.append (v[0]);
    arr.append (v[1]);
    arr.append (v[2]);
    return arr;
  }

  bool vec3_from_json (QJsonArray const& arr, float out[3])
  {
    if (arr.size() != 3)
      return false;
    for (int i = 0; i < 3; ++i)
    {
      if (!arr[i].isDouble())
        return false;
      out[i] = static_cast<float>(arr[i].toDouble());
    }
    return true;
  }

  QJsonObject flicker_to_json (FlickerFields const& f)
  {
    QJsonObject o;
    o["flicker_mode"] = static_cast<int>(f.flicker_mode);
    o["flicker_intensity"] = f.flicker_intensity;
    o["flicker_speed"] = f.flicker_speed;
    o["flicker_seed"] = static_cast<qint64>(f.flicker_seed);
    o["mlta_active"] = f.mlta_active;
    o["mlta_amplitude"] = f.mlta_amplitude;
    o["mlta_frequency"] = f.mlta_frequency;
    o["mlta_function"] = f.mlta_function;
    return o;
  }

  void flicker_from_json (QJsonObject const& o, FlickerFields& f)
  {
    f.flicker_mode = static_cast<std::uint8_t>(std::clamp (o.value ("flicker_mode").toInt (0), 0, 3));
    f.flicker_intensity = static_cast<float>(o.value ("flicker_intensity").toDouble (25.0));
    f.flicker_speed = static_cast<float>(o.value ("flicker_speed").toDouble (15.0));
    if (o.contains ("flicker_seed"))
      f.flicker_seed = static_cast<std::uint32_t>(o.value ("flicker_seed").toVariant().toULongLong());
    else
      f.flicker_seed = 1u;
    f.mlta_active = o.value ("mlta_active").toBool (false);
    f.mlta_amplitude = static_cast<float>(o.value ("mlta_amplitude").toDouble (0.0));
    f.mlta_frequency = static_cast<float>(o.value ("mlta_frequency").toDouble (0.0));
    f.mlta_function = o.value ("mlta_function").toInt (0);
  }

  [[nodiscard]] std::uint8_t read_ngpl_enc_from_adt_file (std::filesystem::path const& adt_path)
  {
    std::ifstream in (adt_path, std::ios::binary);
    if (!in)
      return 0;

    in.seekg (0, std::ios::end);
    auto const sz = in.tellg();
    in.seekg (0, std::ios::beg);
    if (sz <= 0 || sz > 64 * 1024 * 1024)
      return 0;

    std::vector<std::uint8_t> buf (static_cast<std::size_t>(sz));
    in.read (reinterpret_cast<char*>(buf.data()), sz);
    if (!in)
      return 0;

    std::size_t pos = 0;
    while (pos + 8 <= buf.size())
    {
      std::uint32_t fourcc = 0;
      std::uint32_t chsize = 0;
      std::memcpy (&fourcc, buf.data() + pos, 4);
      std::memcpy (&chsize, buf.data() + pos + 4, 4);
      if (fourcc == Noggit::Format::make_fourcc ('N', 'G', 'P', 'L'))
      {
        if (chsize == 4 && pos + 12 <= buf.size())
        {
          std::uint32_t u = 0;
          std::memcpy (&u, buf.data() + pos + 8, 4);
          if (u == 0 || u == 104)
            return 0;
          return static_cast<std::uint8_t>(std::min<std::uint32_t>(u, 255u));
        }
        if (chsize == 256 && pos + 8 + 256 <= buf.size())
        {
          std::uint32_t chunk_max = 104u;
          for (std::size_t i = 0; i < 256; ++i)
          {
            std::uint8_t const b = buf[pos + 8 + i];
            chunk_max = std::max (chunk_max, b == 0 ? 104u : static_cast<std::uint32_t>(b));
          }
          return chunk_max == 104u ? 0 : static_cast<std::uint8_t>(std::min<std::uint32_t>(chunk_max, 255u));
        }
      }
      pos += 8 + chsize;
    }
    return 0;
  }
}

namespace Noggit::MapLights
{
  std::filesystem::path manifest_path_for_map (
    std::filesystem::path const& project_path
  , std::string const& map_basename)
  {
    return project_path / "World" / "Maps" / map_basename / (map_basename + "_lights.json");
  }

  MapLightsManifest build_from_world (std::string const& map_basename, World const* world)
  {
    MapLightsManifest manifest;
    manifest.version = 1;
    manifest.map = map_basename;
    manifest.lgt_mver = MapLightTarget::_lgt_wdt_mver;

    for (auto const& light_in : world->pointLights())
    {
      World::PointLight light = light_in;
      World::syncPointLightTileFromPosition (light);
      glm::vec3 const disk_pos = World::pointLightWorldToDisk (light.position, light.tile_x, light.tile_y);

      FlickerFields flicker {};
      flicker.flicker_mode = light.flicker_mode;
      flicker.flicker_intensity = light.flicker_intensity;
      flicker.flicker_speed = light.flicker_speed;
      flicker.flicker_seed = light.flicker_seed;
      flicker.mlta_active = light.mlta_active;
      flicker.mlta_amplitude = light.mlta_amplitude;
      flicker.mlta_frequency = light.mlta_frequency;
      flicker.mlta_function = light.mlta_function;

      if (light.light_type == World::MapLightType::Spot)
      {
        ManifestSpotLight spot {};
        spot.id = light.id;
        spot.position_disk[0] = disk_pos.x;
        spot.position_disk[1] = disk_pos.y;
        spot.position_disk[2] = disk_pos.z;
        spot.color[0] = light.color.r;
        spot.color[1] = light.color.g;
        spot.color[2] = light.color.b;
        spot.attenuation_start = light.attenuation_start;
        spot.attenuation_end = light.attenuation_end;
        spot.intensity = light.intensity;
        spot.tile_x = light.tile_x;
        spot.tile_y = light.tile_y;
        spot.rotation[0] = light.rotation_radians.x;
        spot.rotation[1] = light.rotation_radians.y;
        spot.rotation[2] = light.rotation_radians.z;
        spot.spotlight_radius = light.spotlight_radius;
        spot.spot_gizmo_scale[0] = light.spot_gizmo_scale.x;
        spot.spot_gizmo_scale[1] = light.spot_gizmo_scale.y;
        spot.spot_gizmo_scale[2] = light.spot_gizmo_scale.z;
        spot.inner_angle = light.inner_angle;
        spot.outer_angle = light.outer_angle;
        spot.cookie_file_data_id = light.cookie_file_data_id;
        spot.flicker_mode = flicker.flicker_mode;
        spot.flicker_intensity = flicker.flicker_intensity;
        spot.flicker_speed = flicker.flicker_speed;
        spot.flicker_seed = flicker.flicker_seed;
        spot.mlta_active = flicker.mlta_active;
        spot.mlta_amplitude = flicker.mlta_amplitude;
        spot.mlta_frequency = flicker.mlta_frequency;
        spot.mlta_function = flicker.mlta_function;
        manifest.spot_lights.push_back (spot);
      }
      else
      {
        ManifestPointLight point {};
        point.id = light.id;
        point.position_disk[0] = disk_pos.x;
        point.position_disk[1] = disk_pos.y;
        point.position_disk[2] = disk_pos.z;
        point.color[0] = light.color.r;
        point.color[1] = light.color.g;
        point.color[2] = light.color.b;
        point.attenuation_start = light.attenuation_start;
        point.attenuation_end = light.attenuation_end;
        point.intensity = light.intensity;
        point.tile_x = light.tile_x;
        point.tile_y = light.tile_y;
        point.rotation[0] = light.rotation_radians.x;
        point.rotation[1] = light.rotation_radians.y;
        point.rotation[2] = light.rotation_radians.z;
        point.mpl3_flags = light.mpl3_flags;
        point.mpl3_scale = light.mpl3_scale;
        point.cookie_file_data_id = light.cookie_file_data_id;
        point.flicker_mode = flicker.flicker_mode;
        point.flicker_intensity = flicker.flicker_intensity;
        point.flicker_speed = flicker.flicker_speed;
        point.flicker_seed = flicker.flicker_seed;
        point.mlta_active = flicker.mlta_active;
        point.mlta_amplitude = flicker.mlta_amplitude;
        point.mlta_frequency = flicker.mlta_frequency;
        point.mlta_function = flicker.mlta_function;
        manifest.point_lights.push_back (point);
      }
    }

    std::set<std::pair<std::uint16_t, std::uint16_t>> cap_tiles;
    for (auto const& light : world->pointLights())
      cap_tiles.emplace (light.tile_x, light.tile_y);

    for (int z = 0; z < 64; ++z)
    {
      for (int x = 0; x < 64; ++x)
      {
        MapTile* const tile = world->mapIndex.getTile (TileIndex (x, z));
        if (tile && tile->adtPointLightCapEncoded() != 0)
          cap_tiles.emplace (static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(z));
      }
    }

    std::filesystem::path const project_path = Noggit::Project::CurrentProject::get()->ProjectPath;
    for (auto const& tz : cap_tiles)
    {
      std::uint8_t enc = 0;
      if (MapTile* const tile = world->mapIndex.getTile (TileIndex (tz.first, tz.second)))
        enc = tile->adtPointLightCapEncoded();
      else
      {
        std::ostringstream adt_name;
        adt_name << map_basename << "_" << static_cast<int>(tz.first) << "_" << static_cast<int>(tz.second) << ".adt";
        std::filesystem::path const adt_path =
          project_path / "World" / "Maps" / map_basename / adt_name.str();
        enc = read_ngpl_enc_from_adt_file (adt_path);
      }

      if (enc != 0)
      {
        manifest.adt_light_caps.push_back (
          { tz.first, tz.second, enc });
      }
    }

    std::sort (manifest.adt_light_caps.begin(), manifest.adt_light_caps.end(),
               [] (ManifestAdtLightCap const& a, ManifestAdtLightCap const& b) {
                 if (a.tile_x != b.tile_x)
                   return a.tile_x < b.tile_x;
                 return a.tile_y < b.tile_y;
               });

    return manifest;
  }

  bool write_json (MapLightsManifest const& manifest, std::filesystem::path const& path)
  {
    QJsonObject root;
    root["version"] = manifest.version;
    root["map"] = QString::fromStdString (manifest.map);
    root["lgt_mver"] = manifest.lgt_mver;

    QJsonArray points;
    for (auto const& p : manifest.point_lights)
    {
      QJsonObject o;
      o["id"] = static_cast<qint64>(p.id);
      o["position_disk"] = vec3_to_json (p.position_disk);
      o["color"] = vec3_to_json (p.color);
      o["attenuation_start"] = p.attenuation_start;
      o["attenuation_end"] = p.attenuation_end;
      o["intensity"] = p.intensity;
      o["tile_x"] = p.tile_x;
      o["tile_y"] = p.tile_y;
      o["rotation"] = vec3_to_json (p.rotation);
      o["mpl3_flags"] = p.mpl3_flags;
      o["mpl3_scale"] = p.mpl3_scale;
      o["cookie_file_data_id"] = static_cast<qint64>(p.cookie_file_data_id);
      QJsonObject const flicker = flicker_to_json (
        { p.flicker_mode, p.flicker_intensity, p.flicker_speed, p.flicker_seed
        , p.mlta_active, p.mlta_amplitude, p.mlta_frequency, p.mlta_function });
      for (auto const& key : flicker.keys())
        o[key] = flicker[key];
      points.append (o);
    }
    root["point_lights"] = points;

    QJsonArray spots;
    for (auto const& s : manifest.spot_lights)
    {
      QJsonObject o;
      o["id"] = static_cast<qint64>(s.id);
      o["position_disk"] = vec3_to_json (s.position_disk);
      o["color"] = vec3_to_json (s.color);
      o["attenuation_start"] = s.attenuation_start;
      o["attenuation_end"] = s.attenuation_end;
      o["intensity"] = s.intensity;
      o["tile_x"] = s.tile_x;
      o["tile_y"] = s.tile_y;
      o["rotation"] = vec3_to_json (s.rotation);
      o["spotlight_radius"] = s.spotlight_radius;
      o["spot_gizmo_scale"] = vec3_to_json (s.spot_gizmo_scale);
      o["inner_angle"] = s.inner_angle;
      o["outer_angle"] = s.outer_angle;
      o["cookie_file_data_id"] = static_cast<qint64>(s.cookie_file_data_id);
      QJsonObject const flicker = flicker_to_json (
        { s.flicker_mode, s.flicker_intensity, s.flicker_speed, s.flicker_seed
        , s.mlta_active, s.mlta_amplitude, s.mlta_frequency, s.mlta_function });
      for (auto const& key : flicker.keys())
        o[key] = flicker[key];
      spots.append (o);
    }
    root["spot_lights"] = spots;

    QJsonArray caps;
    for (auto const& c : manifest.adt_light_caps)
    {
      QJsonObject o;
      o["tile_x"] = c.tile_x;
      o["tile_y"] = c.tile_y;
      o["ngpl_cap_encoded"] = c.ngpl_cap_encoded;
      caps.append (o);
    }
    root["adt_light_caps"] = caps;

    std::error_code ec;
    std::filesystem::create_directories (path.parent_path(), ec);

    QFile out (QString::fromStdString (path.string()));
    if (!out.open (QIODevice::WriteOnly | QIODevice::Truncate))
    {
      LogError << "Map lights JSON: cannot write " << path.string() << std::endl;
      return false;
    }
    out.write (QJsonDocument (root).toJson (QJsonDocument::Indented));
    out.close();
    return true;
  }

  std::optional<MapLightsManifest> read_json (std::filesystem::path const& path)
  {
    QFile in (QString::fromStdString (path.string()));
    if (!in.open (QIODevice::ReadOnly))
      return std::nullopt;

    QJsonParseError err {};
    QJsonDocument const doc = QJsonDocument::fromJson (in.readAll(), &err);
    in.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject())
    {
      LogError << "Map lights JSON: parse error in " << path.string() << ": " << err.errorString().toStdString()
               << std::endl;
      return std::nullopt;
    }

    QJsonObject const root = doc.object();
    MapLightsManifest manifest;
    manifest.version = root.value ("version").toInt (1);
    manifest.map = root.value ("map").toString().toStdString();
    manifest.lgt_mver = root.value ("lgt_mver").toInt (MapLightTarget::_lgt_wdt_mver);

    for (auto const& v : root.value ("point_lights").toArray())
    {
      if (!v.isObject())
        continue;
      QJsonObject o = v.toObject();
      ManifestPointLight p {};
      p.id = static_cast<std::uint32_t>(o.value ("id").toVariant().toULongLong());
      if (!vec3_from_json (o.value ("position_disk").toArray(), p.position_disk)
          || !vec3_from_json (o.value ("color").toArray(), p.color)
          || !vec3_from_json (o.value ("rotation").toArray(), p.rotation))
        continue;
      p.attenuation_start = static_cast<float>(o.value ("attenuation_start").toDouble());
      p.attenuation_end = static_cast<float>(o.value ("attenuation_end").toDouble());
      p.intensity = static_cast<float>(o.value ("intensity").toDouble (1.0));
      p.tile_x = static_cast<std::uint16_t>(o.value ("tile_x").toInt());
      p.tile_y = static_cast<std::uint16_t>(o.value ("tile_y").toInt());
      p.mpl3_flags = static_cast<std::uint16_t>(o.value ("mpl3_flags").toInt());
      p.mpl3_scale = static_cast<float>(o.value ("mpl3_scale").toDouble (0.5));
      p.cookie_file_data_id = static_cast<std::uint32_t>(o.value ("cookie_file_data_id").toVariant().toULongLong());
      FlickerFields flicker {};
      flicker_from_json (o, flicker);
      p.flicker_mode = flicker.flicker_mode;
      p.flicker_intensity = flicker.flicker_intensity;
      p.flicker_speed = flicker.flicker_speed;
      p.flicker_seed = flicker.flicker_seed;
      p.mlta_active = flicker.mlta_active;
      p.mlta_amplitude = flicker.mlta_amplitude;
      p.mlta_frequency = flicker.mlta_frequency;
      p.mlta_function = flicker.mlta_function;
      manifest.point_lights.push_back (p);
    }

    for (auto const& v : root.value ("spot_lights").toArray())
    {
      if (!v.isObject())
        continue;
      QJsonObject o = v.toObject();
      ManifestSpotLight s {};
      s.id = static_cast<std::uint32_t>(o.value ("id").toVariant().toULongLong());
      if (!vec3_from_json (o.value ("position_disk").toArray(), s.position_disk)
          || !vec3_from_json (o.value ("color").toArray(), s.color)
          || !vec3_from_json (o.value ("rotation").toArray(), s.rotation))
        continue;
      s.attenuation_start = static_cast<float>(o.value ("attenuation_start").toDouble());
      s.attenuation_end = static_cast<float>(o.value ("attenuation_end").toDouble());
      s.intensity = static_cast<float>(o.value ("intensity").toDouble (1.0));
      s.tile_x = static_cast<std::uint16_t>(o.value ("tile_x").toInt());
      s.tile_y = static_cast<std::uint16_t>(o.value ("tile_y").toInt());
      if (!vec3_from_json (o.value ("spot_gizmo_scale").toArray(), s.spot_gizmo_scale))
      {
        s.spot_gizmo_scale[0] = s.spot_gizmo_scale[1] = s.spot_gizmo_scale[2] = 1.f;
      }
      s.spotlight_radius = static_cast<float>(o.value ("spotlight_radius").toDouble (15.0));
      s.inner_angle = static_cast<float>(o.value ("inner_angle").toDouble());
      s.outer_angle = static_cast<float>(o.value ("outer_angle").toDouble());
      s.cookie_file_data_id = static_cast<std::uint32_t>(o.value ("cookie_file_data_id").toVariant().toULongLong());
      FlickerFields flicker {};
      flicker_from_json (o, flicker);
      s.flicker_mode = flicker.flicker_mode;
      s.flicker_intensity = flicker.flicker_intensity;
      s.flicker_speed = flicker.flicker_speed;
      s.flicker_seed = flicker.flicker_seed;
      s.mlta_active = flicker.mlta_active;
      s.mlta_amplitude = flicker.mlta_amplitude;
      s.mlta_frequency = flicker.mlta_frequency;
      s.mlta_function = flicker.mlta_function;
      manifest.spot_lights.push_back (s);
    }

    for (auto const& v : root.value ("adt_light_caps").toArray())
    {
      if (!v.isObject())
        continue;
      QJsonObject o = v.toObject();
      ManifestAdtLightCap c {};
      c.tile_x = static_cast<std::uint16_t>(o.value ("tile_x").toInt());
      c.tile_y = static_cast<std::uint16_t>(o.value ("tile_y").toInt());
      c.ngpl_cap_encoded = static_cast<std::uint8_t>(o.value ("ngpl_cap_encoded").toInt());
      if (c.ngpl_cap_encoded != 0)
        manifest.adt_light_caps.push_back (c);
    }

    return manifest;
  }

  bool build_lgt_buffer (MapLightsManifest const& manifest, std::vector<std::uint8_t>& out)
  {
    std::vector<MltaRow> mlta_out;
    std::vector<MPL3Record> mpl3_records;
    std::vector<MSLTRecord> mslt_records;
    std::vector<bool> mpl3_legacy_texture_fields;
    std::vector<std::uint32_t> mpl3_cookie_fdid;
    std::vector<std::uint32_t> mslt_cookie_fdid;

    for (auto const& light : manifest.point_lights)
    {
      MPL3Record rec {};
      rec.light_index = light.id;
      rec.color_bgra[0] = static_cast<std::uint8_t>(std::clamp (light.color[2], 0.f, 1.f) * 255.f);
      rec.color_bgra[1] = static_cast<std::uint8_t>(std::clamp (light.color[1], 0.f, 1.f) * 255.f);
      rec.color_bgra[2] = static_cast<std::uint8_t>(std::clamp (light.color[0], 0.f, 1.f) * 255.f);

      FlickerFields flicker { light.flicker_mode, light.flicker_intensity, light.flicker_speed, light.flicker_seed
                            , light.mlta_active, light.mlta_amplitude, light.mlta_frequency, light.mlta_function };
      std::int16_t const mi = alloc_mlta (flicker, mlta_out);
      bool legacy_seed_flicker = false;
      if (mi >= 0)
      {
        rec.color_bgra[3] = 0;
        rec.rotation[0] = light.rotation[0];
        rec.rotation[1] = light.rotation[1];
        rec.rotation[2] = light.rotation[2];
        rec.mlta_index = mi;
        rec.texture_index = -1;
      }
      else if (light.flicker_mode != 0)
      {
        legacy_seed_flicker = true;
        rec.color_bgra[3] = light.flicker_mode;
        rec.rotation[0] = light.flicker_speed;
        rec.rotation[1] = light.flicker_intensity;
        rec.rotation[2] = 0.f;
        rec.mlta_index = static_cast<std::int16_t>(light.flicker_seed & 0xFFFFu);
        rec.texture_index = static_cast<std::int16_t>((light.flicker_seed >> 16) & 0xFFFFu);
      }
      else
      {
        rec.color_bgra[3] = 0;
        rec.rotation[0] = light.rotation[0];
        rec.rotation[1] = light.rotation[1];
        rec.rotation[2] = light.rotation[2];
        rec.mlta_index = -1;
        rec.texture_index = -1;
      }

      rec.position[0] = light.position_disk[0];
      rec.position[1] = light.position_disk[1];
      rec.position[2] = light.position_disk[2];
      rec.attenuation_start = light.attenuation_start;
      rec.attenuation_end = light.attenuation_end;
      clamp_attenuation (rec.attenuation_start, rec.attenuation_end);
      rec.intensity = light.intensity;
      rec.tile_x = light.tile_x;
      rec.tile_y = light.tile_y;
      rec.flags = light.mpl3_flags;
      rec.scale_half = float_to_half_bits (light.mpl3_scale);

      mpl3_legacy_texture_fields.push_back (legacy_seed_flicker);
      mpl3_cookie_fdid.push_back (legacy_seed_flicker ? 0u : light.cookie_file_data_id);
      mpl3_records.push_back (rec);
    }

    for (auto const& light : manifest.spot_lights)
    {
      MSLTRecord rec {};
      rec.id = light.id;
      rec.color_bgra[0] = static_cast<std::uint8_t>(std::clamp (light.color[2], 0.f, 1.f) * 255.f);
      rec.color_bgra[1] = static_cast<std::uint8_t>(std::clamp (light.color[1], 0.f, 1.f) * 255.f);
      rec.color_bgra[2] = static_cast<std::uint8_t>(std::clamp (light.color[0], 0.f, 1.f) * 255.f);
      rec.color_bgra[3] = 0;

      FlickerFields flicker { light.flicker_mode, light.flicker_intensity, light.flicker_speed, light.flicker_seed
                            , light.mlta_active, light.mlta_amplitude, light.mlta_frequency, light.mlta_function };
      std::int16_t const mi = alloc_mlta (flicker, mlta_out);

      rec.position[0] = light.position_disk[0];
      rec.position[1] = light.position_disk[1];
      rec.position[2] = light.position_disk[2];
      rec.attenuation_start = light.attenuation_start;
      rec.attenuation_end = light.attenuation_end;
      clamp_attenuation (rec.attenuation_start, rec.attenuation_end);
      rec.intensity = light.intensity;
      rec.rotation[0] = light.rotation[0];
      rec.rotation[1] = light.rotation[1];
      rec.rotation[2] = light.rotation[2];
      float const mx = std::max ({ light.spot_gizmo_scale[0], light.spot_gizmo_scale[1], light.spot_gizmo_scale[2] });
      rec.spotlight_radius = std::max (0.f, light.spotlight_radius * mx);
      rec.inner_angle = light.inner_angle;
      rec.outer_angle = light.outer_angle;
      if (rec.inner_angle > rec.outer_angle)
        std::swap (rec.inner_angle, rec.outer_angle);
      rec.tile_x = light.tile_x;
      rec.tile_y = light.tile_y;
      rec.mlta_index = mi;
      rec.texture_index = -1;
      mslt_records.push_back (rec);
      mslt_cookie_fdid.push_back (light.cookie_file_data_id);
    }

    std::set<std::uint32_t> mtex_unique;
    for (std::uint32_t const fd : mpl3_cookie_fdid)
      if (fd)
        mtex_unique.insert (fd);
    for (std::uint32_t const fd : mslt_cookie_fdid)
      if (fd)
        mtex_unique.insert (fd);
    std::vector<std::uint32_t> mtex_sorted (mtex_unique.begin(), mtex_unique.end());

    auto const remap_cookie = [&] (std::uint32_t fd) -> std::int16_t {
      if (!fd)
        return static_cast<std::int16_t>(-1);
      auto const it = std::lower_bound (mtex_sorted.begin(), mtex_sorted.end(), fd);
      if (it == mtex_sorted.end() || *it != fd)
        return static_cast<std::int16_t>(-1);
      return static_cast<std::int16_t>(it - mtex_sorted.begin());
    };

    for (std::size_t i = 0; i < mpl3_records.size(); ++i)
    {
      if (mpl3_legacy_texture_fields[i])
        continue;
      mpl3_records[i].texture_index = remap_cookie (mpl3_cookie_fdid[i]);
    }
    for (std::size_t i = 0; i < mslt_records.size(); ++i)
      mslt_records[i].texture_index = remap_cookie (mslt_cookie_fdid[i]);

    util::sExtendableArray lgtFile;
    int curPos = 0;

    lgtFile.Extend (8 + 0x4);
    SetChunkHeader (lgtFile, curPos, 'MVER', 4);
    *(lgtFile.GetPointer<int>(8)) = manifest.lgt_mver;
    curPos += 8 + 0x4;

    if (!mpl3_records.empty())
    {
      lgtFile.Extend (8);
      SetChunkHeader (lgtFile, curPos, 'MPL3', static_cast<int>(mpl3_records.size() * sizeof (MPL3Record)));
      curPos += 8;
      lgtFile.Insert (curPos
                     , static_cast<unsigned long>(mpl3_records.size() * sizeof (MPL3Record))
                     , reinterpret_cast<char*>(mpl3_records.data()));
      curPos += static_cast<int>(mpl3_records.size() * sizeof (MPL3Record));
    }

    if (!mslt_records.empty())
    {
      lgtFile.Extend (8);
      SetChunkHeader (lgtFile, curPos, 'MSLT', static_cast<int>(mslt_records.size() * sizeof (MSLTRecord)));
      curPos += 8;
      lgtFile.Insert (curPos
                     , static_cast<unsigned long>(mslt_records.size() * sizeof (MSLTRecord))
                     , reinterpret_cast<char*>(mslt_records.data()));
      curPos += static_cast<int>(mslt_records.size() * sizeof (MSLTRecord));
    }

    if (!mtex_sorted.empty())
    {
      int const mtex_bytes = static_cast<int>(mtex_sorted.size() * sizeof (std::uint32_t));
      lgtFile.Extend (8 + mtex_bytes);
      SetChunkHeader (lgtFile, curPos, 'MTEX', mtex_bytes);
      curPos += 8;
      for (std::uint32_t const id : mtex_sorted)
      {
        *lgtFile.GetPointer<std::uint32_t>(curPos) = id;
        curPos += static_cast<int>(sizeof (std::uint32_t));
      }
    }

    if (!mlta_out.empty())
    {
      int const bytes = static_cast<int>(mlta_out.size() * (sizeof (float) * 2 + sizeof (int)));
      lgtFile.Extend (8 + bytes);
      SetChunkHeader (lgtFile, curPos, 'MLTA', bytes);
      curPos += 8;
      for (auto const& row : mlta_out)
      {
        *lgtFile.GetPointer<float>(curPos) = row.amplitude;
        curPos += sizeof (float);
        *lgtFile.GetPointer<float>(curPos) = row.frequency;
        curPos += sizeof (float);
        *lgtFile.GetPointer<int>(curPos) = row.function;
        curPos += sizeof (int);
      }
    }

    auto const raw = lgtFile.all_data();
    out.assign (reinterpret_cast<std::uint8_t const*>(raw.data())
               , reinterpret_cast<std::uint8_t const*>(raw.data()) + raw.size());
    return true;
  }

  bool write_lgt_wdt (MapLightsManifest const& manifest, std::filesystem::path const& path)
  {
    std::vector<std::uint8_t> buf;
    if (!build_lgt_buffer (manifest, buf))
      return false;

    std::error_code ec;
    std::filesystem::create_directories (path.parent_path(), ec);

    std::ofstream out (path, std::ios::binary | std::ios::trunc);
    if (!out)
    {
      LogError << "Map lights: cannot write " << path.string() << std::endl;
      return false;
    }
    out.write (reinterpret_cast<char const*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    return static_cast<bool>(out);
  }

  bool verify_lgt_round_trip (MapLightsManifest const& manifest, std::vector<std::uint8_t> const& written_lgt)
  {
    std::vector<std::uint8_t> rebuilt;
    if (!build_lgt_buffer (manifest, rebuilt))
    {
      LogError << "Map lights: failed to rebuild _lgt.wdt for round-trip check." << std::endl;
      return false;
    }

    if (rebuilt != written_lgt)
    {
      LogError << "Map lights: _lgt.wdt round-trip mismatch (written " << written_lgt.size()
               << " bytes, rebuilt " << rebuilt.size() << " bytes)." << std::endl;
      return false;
    }

    return true;
  }
}
