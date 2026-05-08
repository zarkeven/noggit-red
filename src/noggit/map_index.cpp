// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <math/coordinates.hpp>
#include <noggit/AsyncLoader.h>
#include <noggit/MapChunk.h>
#include <noggit/MapTile.h>
#include <noggit/Misc.h>
#include <noggit/World.h>
#include <noggit/ActionManager.hpp>
#include <noggit/Action.hpp>
#include <noggit/project/CurrentProject.hpp>
#ifdef USE_MYSQL_UID_STORAGE
  #include <mysql/mysql.h>
#endif
#include <noggit/map_index.hpp>
#include <noggit/map_light_target.hpp>
#include <noggit/Log.h>
#include <noggit/uid_storage.hpp>
#include <noggit/application/NoggitApplication.hpp>
#include <ClientFile.hpp>
#include <ClientData.hpp>
#include <Listfile.hpp>

#include <QtCore/QSettings>
#include <QCheckBox>
#include <QMessageBox>
#include <QString>
#include <QByteArray>
#include <QTextStream>
#include <QRegExp>
#include <QFile>

#include <fstream>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <forward_list>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <sstream>
#include <vector>

namespace
{
#pragma pack(push, 1)
  struct MPL2Record
  {
    std::uint32_t id;
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
  };
  static_assert(sizeof(MPL2Record) == 0x34, "MPL2 record must be 0x34 bytes");

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

  float half_bits_to_float (std::uint16_t h)
  {
    std::uint32_t const sign = static_cast<std::uint32_t>(h & 0x8000u) << 16;
    int const exp = (h >> 10) & 0x1f;
    std::uint32_t const mant = h & 0x3ffu;
    std::uint32_t f;
    if (exp == 0)
      f = sign | (mant << 13);
    else if (exp == 31)
      f = sign | 0x7f800000u | (mant << 13);
    else
      f = sign | static_cast<std::uint32_t>(exp - 15 + 127) << 23 | (mant << 13);
    float out;
    std::memcpy (&out, &f, sizeof (float));
    return out;
  }

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

  void apply_mlta_to_light (World::PointLight& light, std::int16_t idx, std::vector<MltaRow> const& mlta_rows)
  {
    if (idx < 0 || static_cast<std::size_t>(idx) >= mlta_rows.size())
      return;

    MltaRow const& row = mlta_rows[static_cast<std::size_t>(idx)];
    if (row.function <= 0)
      return;

    light.mlta_active = true;
    light.mlta_amplitude = row.amplitude;
    light.mlta_frequency = row.frequency;
    light.mlta_function = row.function;
    light.flicker_mode = static_cast<std::uint8_t>(std::clamp (row.function, 0, 3));
    light.flicker_intensity = row.amplitude;
    light.flicker_speed = row.frequency;
  }

  //! Cleared when Noggit exits; suppresses MPL2 vs NGPL overflow QMessageBox for the rest of the session.
  bool suppress_point_light_mpl2_overflow_dialog = false;

  [[nodiscard]] bool client_uses_modern_wdt_mphd()
  {
    auto* app = Noggit::Application::NoggitApplication::instance();
    if (!app || !app->hasClientData())
      return false;
    return app->clientData()->version() != BlizzardArchive::ClientVersion::WOTLK;
  }

  //! MPL2 lives in `_lgt.wdt`; external tools often watch `.adt` mtimes. Bump each ADT that has lights so watchers refresh.
  void bump_adt_write_times_for_point_lights(std::string const& basename, World* world)
  {
    BlizzardArchive::ClientData* const client_data = Noggit::Application::NoggitApplication::instance()->clientData();
    if (!client_data)
      return;

    std::set<std::pair<std::uint16_t, std::uint16_t>> adt_tiles;
    for (auto const& light : world->pointLights())
      adt_tiles.emplace(light.tile_x, light.tile_y);

    namespace fs = std::filesystem;

    for (auto const& tz : adt_tiles)
    {
      std::ostringstream adt_rel;
      adt_rel << "World\\Maps\\" << basename << "\\" << basename << "_"
              << static_cast<int>(tz.first) << "_" << static_cast<int>(tz.second) << ".adt";
      BlizzardArchive::Listfile::FileKey const key(
        adt_rel.str(),
        const_cast<BlizzardArchive::Listfile::Listfile*>(client_data->listfile()));
      std::string const disk = client_data->getDiskPath(key);
      if (disk.empty())
        continue;

      std::error_code ec;
      fs::path const p(disk);
      if (!fs::exists(p) || !fs::is_regular_file(p, ec) || ec)
        continue;

      fs::last_write_time(p, std::chrono::file_clock::now(), ec);
    }
  }

  void loadPointLightsFromLgtWdt(std::string const& basename, World* world)
  {
    std::stringstream filename;
    filename << "World\\Maps\\" << basename << "\\" << basename << "_lgt.wdt";
    std::string const rel_win = filename.str();
    std::string const rel_disk = BlizzardArchive::ClientData::normalizeFilenameInternal (rel_win);

    BlizzardArchive::ClientData* const cd = Noggit::Application::NoggitApplication::instance()->clientData();
    if (!cd)
      return;

    std::string open_key = rel_disk;
    if (!cd->exists (open_key) && cd->exists (rel_win))
      open_key = rel_win;
    else if (!cd->exists (open_key))
      return;

    BlizzardArchive::ClientFile file(open_key, cd);
    if (file.isEof())
      return;

    uint32_t fourcc = 0;
    uint32_t size = 0;

    std::vector<MPL2Record> mpl2_recs;
    std::vector<MPL3Record> mpl3_recs;
    std::vector<MSLTRecord> mslt_recs;
    std::vector<MltaRow> mlta_rows;
    std::vector<std::uint32_t> mtex_ids;

    while (!file.isEof())
    {
      if (file.read(&fourcc, 4) != 4)
        break;
      if (file.read(&size, 4) != 4)
        break;

      if (fourcc == 'MVER')
      {
        file.seekRelative(size);
        continue;
      }

      if (fourcc == 'MLTA')
      {
        std::size_t const n = size / (sizeof (float) * 2 + sizeof (int));
        for (std::size_t i = 0; i < n; ++i)
        {
          MltaRow row{};
          file.read (&row.amplitude, sizeof (float));
          file.read (&row.frequency, sizeof (float));
          file.read (&row.function, sizeof (int));
          mlta_rows.push_back (row);
        }
        std::size_t const rem = size % (sizeof (float) * 2 + sizeof (int));
        if (rem)
          file.seekRelative (rem);
        continue;
      }

      if (fourcc == 'MTEX')
      {
        std::size_t const count = size / sizeof (std::uint32_t);
        mtex_ids.resize (count);
        if (count)
          file.read (mtex_ids.data(), static_cast<int>(size));
        else
        {}
        continue;
      }

      if (fourcc == 'MPL2')
      {
        std::size_t const count = size / sizeof (MPL2Record);
        mpl2_recs.resize (count);
        if (count)
          file.read (mpl2_recs.data(), static_cast<int>(count * sizeof (MPL2Record)));
        std::size_t const remainder = size % sizeof (MPL2Record);
        if (remainder)
          file.seekRelative (remainder);
        continue;
      }

      if (fourcc == 'MPL3')
      {
        std::size_t const count = size / sizeof (MPL3Record);
        mpl3_recs.resize (count);
        if (count)
          file.read (mpl3_recs.data(), static_cast<int>(count * sizeof (MPL3Record)));
        std::size_t const remainder = size % sizeof (MPL3Record);
        if (remainder)
          file.seekRelative (remainder);
        continue;
      }

      if (fourcc == 'MSLT')
      {
        std::size_t const count = size / sizeof (MSLTRecord);
        mslt_recs.resize (count);
        if (count)
          file.read (mslt_recs.data(), static_cast<int>(count * sizeof (MSLTRecord)));
        std::size_t const remainder = size % sizeof (MSLTRecord);
        if (remainder)
          file.seekRelative (remainder);
        continue;
      }

      file.seekRelative(size);
    }

    file.close();

    world->pointLights().clear();

    auto const mpl2_to_light = [&] (MPL2Record const& rec) {
      World::PointLight light{};
      light.light_type = World::MapLightType::Point;
      light.id = rec.id;
      light.position = { rec.position[0], rec.position[1], rec.position[2] };
      light.color = { float(rec.color_bgra[2]) / 255.f
                    , float(rec.color_bgra[1]) / 255.f
                    , float(rec.color_bgra[0]) / 255.f };
      light.attenuation_start = rec.attenuation_start;
      light.attenuation_end = rec.attenuation_end;
      light.intensity = rec.intensity;
      light.tile_x = rec.tile_x;
      light.tile_y = rec.tile_y;
      light.rotation_radians = { rec.rotation[0], rec.rotation[1], rec.rotation[2] };
      light.mlta_index = rec.mlta_index;
      light.texture_index = rec.texture_index;

      light.flicker_mode = rec.color_bgra[3];
      if (light.flicker_mode > 3)
        light.flicker_mode = 0;

      bool const mlta_ok = rec.mlta_index >= 0
        && static_cast<std::size_t>(rec.mlta_index) < mlta_rows.size()
        && mlta_rows[static_cast<std::size_t>(rec.mlta_index)].function > 0;

      if (mlta_ok)
      {
        apply_mlta_to_light (light, rec.mlta_index, mlta_rows);
      }
      else if (light.flicker_mode != 0)
      {
        light.mlta_active = false;
        light.flicker_speed = rec.rotation[0];
        light.flicker_intensity = rec.rotation[1];
        auto const lo = static_cast<std::uint32_t>(static_cast<std::uint16_t>(rec.mlta_index));
        auto const hi = static_cast<std::uint32_t>(static_cast<std::uint16_t>(rec.texture_index));
        light.flicker_seed = (hi << 16) | lo;
        if (light.flicker_seed == 0)
          light.flicker_seed = rec.id * 2654435761u + 1u;
      }

      bool const legacy_packed = !mlta_ok && light.flicker_mode != 0;
      if (!legacy_packed && rec.texture_index >= 0
          && static_cast<std::size_t>(rec.texture_index) < mtex_ids.size())
        light.cookie_file_data_id = mtex_ids[static_cast<std::size_t>(rec.texture_index)];
      light.texture_index = -1;
      return light;
    };

    if (!mpl3_recs.empty())
    {
      for (auto const& rec : mpl3_recs)
      {
        World::PointLight light{};
        light.light_type = World::MapLightType::Point;
        light.id = rec.light_index;
        light.position = { rec.position[0], rec.position[1], rec.position[2] };
        light.color = { float(rec.color_bgra[2]) / 255.f
                      , float(rec.color_bgra[1]) / 255.f
                      , float(rec.color_bgra[0]) / 255.f };
        light.attenuation_start = rec.attenuation_start;
        light.attenuation_end = rec.attenuation_end;
        light.intensity = rec.intensity;
        light.tile_x = rec.tile_x;
        light.tile_y = rec.tile_y;
        light.rotation_radians = { rec.rotation[0], rec.rotation[1], rec.rotation[2] };
        light.mlta_index = rec.mlta_index;
        light.texture_index = rec.texture_index;
        light.mpl3_flags = rec.flags;
        light.mpl3_scale = half_bits_to_float (rec.scale_half);

        light.flicker_mode = rec.color_bgra[3];
        if (light.flicker_mode > 3)
          light.flicker_mode = 0;

        bool const mlta_ok = rec.mlta_index >= 0
          && static_cast<std::size_t>(rec.mlta_index) < mlta_rows.size()
          && mlta_rows[static_cast<std::size_t>(rec.mlta_index)].function > 0;

        if (mlta_ok)
          apply_mlta_to_light (light, rec.mlta_index, mlta_rows);
        else if (light.flicker_mode != 0)
        {
          light.mlta_active = false;
          light.flicker_speed = rec.rotation[0];
          light.flicker_intensity = rec.rotation[1];
          auto const lo = static_cast<std::uint32_t>(static_cast<std::uint16_t>(rec.mlta_index));
          auto const hi = static_cast<std::uint32_t>(static_cast<std::uint16_t>(rec.texture_index));
          light.flicker_seed = (hi << 16) | lo;
          if (light.flicker_seed == 0)
            light.flicker_seed = rec.light_index * 2654435761u + 1u;
        }

        bool const legacy_packed = !mlta_ok && light.flicker_mode != 0;
        if (!legacy_packed && rec.texture_index >= 0
            && static_cast<std::size_t>(rec.texture_index) < mtex_ids.size())
          light.cookie_file_data_id = mtex_ids[static_cast<std::size_t>(rec.texture_index)];
        light.texture_index = -1;

        world->pointLights().push_back (light);
      }
    }
    else
    {
      for (auto const& rec : mpl2_recs)
        world->pointLights().push_back (mpl2_to_light (rec));
    }

    for (auto const& rec : mslt_recs)
    {
      World::PointLight light{};
      light.light_type = World::MapLightType::Spot;
      light.id = rec.id;
      light.position = { rec.position[0], rec.position[1], rec.position[2] };
      light.color = { float(rec.color_bgra[2]) / 255.f
                    , float(rec.color_bgra[1]) / 255.f
                    , float(rec.color_bgra[0]) / 255.f };
      light.attenuation_start = rec.attenuation_start;
      light.attenuation_end = rec.attenuation_end;
      light.intensity = rec.intensity;
      light.tile_x = rec.tile_x;
      light.tile_y = rec.tile_y;
      light.rotation_radians = { rec.rotation[0], rec.rotation[1], rec.rotation[2] };
      light.spotlight_radius = rec.spotlight_radius;
      light.inner_angle = rec.inner_angle;
      light.outer_angle = rec.outer_angle;
      light.mlta_index = rec.mlta_index;
      light.texture_index = rec.texture_index;

      light.flicker_mode = rec.color_bgra[3];
      if (light.flicker_mode > 3)
        light.flicker_mode = 0;

      bool const mlta_ok = rec.mlta_index >= 0
        && static_cast<std::size_t>(rec.mlta_index) < mlta_rows.size()
        && mlta_rows[static_cast<std::size_t>(rec.mlta_index)].function > 0;

      if (mlta_ok)
        apply_mlta_to_light (light, rec.mlta_index, mlta_rows);
      else
      {
        // Rotation encodes spot orientation; do not unpack legacy MPL2 flicker from it.
        light.flicker_mode = 0;
        light.mlta_active = false;
      }

      if (rec.texture_index >= 0
          && static_cast<std::size_t>(rec.texture_index) < mtex_ids.size())
        light.cookie_file_data_id = mtex_ids[static_cast<std::size_t>(rec.texture_index)];
      light.texture_index = -1;

      world->pointLights().push_back (light);
    }
  }

  void savePointLightsToLgtWdt(std::string const& basename, World* world)
  {
    std::stringstream filename;
    filename << "World\\Maps\\" << basename << "\\" << basename << "_lgt.wdt";
    std::string const rel_win = filename.str();
    std::string const rel_disk = BlizzardArchive::ClientData::normalizeFilenameInternal (rel_win);

    auto clamp_attenuation = [] (float& a_start, float& a_end)
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
    };

    std::vector<MltaRow> mlta_out;
    auto alloc_mlta = [&] (World::PointLight const& light) -> std::int16_t {
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
    };

    std::vector<MPL3Record> mpl3_records;
    std::vector<MSLTRecord> mslt_records;
    std::vector<bool> mpl3_legacy_texture_fields;
    std::vector<std::uint32_t> mpl3_cookie_fdid;
    std::vector<std::uint32_t> mslt_cookie_fdid;
    mpl3_records.reserve (world->pointLights().size());
    mslt_records.reserve (world->pointLights().size());
    mpl3_legacy_texture_fields.reserve (world->pointLights().size());
    mpl3_cookie_fdid.reserve (world->pointLights().size());
    mslt_cookie_fdid.reserve (world->pointLights().size());

    for (auto const& light : world->pointLights())
    {
      if (light.light_type == World::MapLightType::Spot)
      {
        MSLTRecord rec{};
        rec.id = light.id;
        rec.color_bgra[0] = static_cast<std::uint8_t>(std::clamp(light.color.b, 0.f, 1.f) * 255.f);
        rec.color_bgra[1] = static_cast<std::uint8_t>(std::clamp(light.color.g, 0.f, 1.f) * 255.f);
        rec.color_bgra[2] = static_cast<std::uint8_t>(std::clamp(light.color.r, 0.f, 1.f) * 255.f);
        rec.color_bgra[3] = 0;

        std::int16_t const mi = alloc_mlta (light);

        rec.position[0] = light.position.x;
        rec.position[1] = light.position.y;
        rec.position[2] = light.position.z;
        rec.attenuation_start = light.attenuation_start;
        rec.attenuation_end = light.attenuation_end;
        clamp_attenuation(rec.attenuation_start, rec.attenuation_end);
        rec.intensity = light.intensity;
        rec.rotation[0] = light.rotation_radians.x;
        rec.rotation[1] = light.rotation_radians.y;
        rec.rotation[2] = light.rotation_radians.z;
        {
          float const mx = std::max ({ light.spot_gizmo_scale.x, light.spot_gizmo_scale.y, light.spot_gizmo_scale.z });
          rec.spotlight_radius = std::max(0.f, light.spotlight_radius * mx);
        }
        rec.inner_angle = light.inner_angle;
        rec.outer_angle = light.outer_angle;
        if (rec.inner_angle > rec.outer_angle)
          std::swap(rec.inner_angle, rec.outer_angle);
        rec.tile_x = light.tile_x;
        rec.tile_y = light.tile_y;
        rec.mlta_index = mi;
        rec.texture_index = -1;
        mslt_records.push_back (rec);
        mslt_cookie_fdid.push_back (light.cookie_file_data_id);
      }
      else
      {
        MPL3Record rec{};
        rec.light_index = light.id;
        rec.color_bgra[0] = static_cast<std::uint8_t>(std::clamp(light.color.b, 0.f, 1.f) * 255.f);
        rec.color_bgra[1] = static_cast<std::uint8_t>(std::clamp(light.color.g, 0.f, 1.f) * 255.f);
        rec.color_bgra[2] = static_cast<std::uint8_t>(std::clamp(light.color.r, 0.f, 1.f) * 255.f);

        std::int16_t const mi = alloc_mlta (light);
        bool legacy_seed_flicker = false;
        if (mi >= 0)
        {
          rec.color_bgra[3] = 0;
          rec.rotation[0] = light.rotation_radians.x;
          rec.rotation[1] = light.rotation_radians.y;
          rec.rotation[2] = light.rotation_radians.z;
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
          rec.rotation[0] = light.rotation_radians.x;
          rec.rotation[1] = light.rotation_radians.y;
          rec.rotation[2] = light.rotation_radians.z;
          rec.mlta_index = -1;
          rec.texture_index = -1;
        }

        rec.position[0] = light.position.x;
        rec.position[1] = light.position.y;
        rec.position[2] = light.position.z;
        rec.attenuation_start = light.attenuation_start;
        rec.attenuation_end = light.attenuation_end;
        clamp_attenuation(rec.attenuation_start, rec.attenuation_end);
        rec.intensity = light.intensity;
        rec.tile_x = light.tile_x;
        rec.tile_y = light.tile_y;
        rec.flags = light.mpl3_flags;
        rec.scale_half = float_to_half_bits (light.mpl3_scale);

        mpl3_legacy_texture_fields.push_back (legacy_seed_flicker);
        mpl3_cookie_fdid.push_back (legacy_seed_flicker ? 0u : light.cookie_file_data_id);
        mpl3_records.push_back (rec);
      }
    }

    std::set<std::uint32_t> mtex_unique;
    for (std::uint32_t const fd : mpl3_cookie_fdid)
    {
      if (fd)
        mtex_unique.insert (fd);
    }
    for (std::uint32_t const fd : mslt_cookie_fdid)
    {
      if (fd)
        mtex_unique.insert (fd);
    }
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

    lgtFile.Extend(8 + 0x4);
    SetChunkHeader(lgtFile, curPos, 'MVER', 4);
    *(lgtFile.GetPointer<int>(8)) = Noggit::MapLightTarget::_lgt_wdt_mver;
    curPos += 8 + 0x4;

    // Chunk order: MVER, then MPL3, MSLT, MTEX, MLTA as in retail (omit chunks with no body — see
    // e.g. rymoore_lgt.wdt: MVER+MPL3+MLTA only when there are no spots or MTEX cookies).

    if (!mpl3_records.empty())
    {
      lgtFile.Extend(8);
      SetChunkHeader(lgtFile, curPos, 'MPL3', static_cast<int>(mpl3_records.size() * sizeof (MPL3Record)));
      curPos += 8;
      lgtFile.Insert (curPos
                     , static_cast<unsigned long>(mpl3_records.size() * sizeof (MPL3Record))
                     , reinterpret_cast<char*>(mpl3_records.data()));
      curPos += static_cast<int>(mpl3_records.size() * sizeof (MPL3Record));
    }

    if (!mslt_records.empty())
    {
      lgtFile.Extend(8);
      SetChunkHeader(lgtFile, curPos, 'MSLT', static_cast<int>(mslt_records.size() * sizeof (MSLTRecord)));
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
      lgtFile.Extend(8 + bytes);
      SetChunkHeader(lgtFile, curPos, 'MLTA', bytes);
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

    BlizzardArchive::ClientData* const cd = Noggit::Application::NoggitApplication::instance()->clientData();
    if (!cd)
      return;

    namespace fs = std::filesystem;
    fs::path const out_path = fs::path (cd->projectPath()) / rel_disk;
    std::error_code ec;
    fs::create_directories (out_path.parent_path(), ec);
    std::ofstream out (out_path.string(), std::ios::binary | std::ios::trunc);
    if (!out)
    {
      LogError << "savePointLightsToLgtWdt: failed to open " << out_path.string() << std::endl;
      return;
    }
    std::vector<char> const& bytes = lgtFile.all_data();
    out.write (bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out)
      LogError << "savePointLightsToLgtWdt: short write " << out_path.string() << std::endl;

    bump_adt_write_times_for_point_lights(basename, world);

    if (!suppress_point_light_mpl2_overflow_dialog)
    {
      if (std::string const report = world->pointLightMpl2SaveOverflowReport(); !report.empty())
      {
        QMessageBox box(QMessageBox::Warning,
                        QStringLiteral("Point lights"),
                        QString::fromStdString(report),
                        QMessageBox::Ok,
                        nullptr);
        QCheckBox dont_show(QStringLiteral("Don't show this warning again this session"));
        box.setCheckBox(&dont_show);
        box.exec();
        if (dont_show.isChecked())
          suppress_point_light_mpl2_overflow_dialog = true;
      }
    }
  }
}

MapIndex::TileRange<false> MapIndex::loaded_tiles()
{
  return tiles<false>
    ([](TileIndex const&, MapTile* tile) { return !!tile && tile->finishedLoading(); });
}

MapIndex::TileRange<true> MapIndex::tiles_in_range(glm::vec3 const& pos, float radius)
{
  return tiles<true>
    ([this, pos, radius](TileIndex const& index, MapTile*)
      {
        return hasTile(index) && misc::getShortestDist
        (pos.x, pos.z, index.x * TILESIZE, index.z * TILESIZE, TILESIZE) <= radius;
      }
    );
}

MapIndex::TileRange<true> MapIndex::tiles_in_rect(glm::vec3 const& pos, float radius)
{
  glm::vec2 l_chunk{ pos.x - radius, pos.z - radius };
  glm::vec2 r_chunk{ pos.x + radius, pos.z + radius };

  return tiles<true>
    ([this, pos, radius, l_chunk, r_chunk](TileIndex const& index, MapTile*)
      {
        if (!hasTile(index) || radius == 0.f)
          return false;

        glm::vec2 l_tile{ index.x * TILESIZE, index.z * TILESIZE };
        glm::vec2 r_tile{ index.x * TILESIZE + TILESIZE, index.z * TILESIZE + TILESIZE };

        return ((l_chunk.x  <  r_tile.x) && (r_chunk.x >= l_tile.x) && (l_chunk.y  <  r_tile.y) && (r_chunk.y >= l_tile.y));
      }
    );
}

MapIndex::MapIndex (const std::string &pBasename, int map_id, World* world,
                    Noggit::NoggitRenderContext context, bool create_empty)
  : basename(pBasename)
  , _map_id (map_id)
  , _last_unload_time((clock() / CLOCKS_PER_SEC)) // to not try to unload right away
  , mBigAlpha(false)
  , mHasAGlobalWMO(false)
  , changed(false)
  , _sort_models_by_size_class(false)
  , highestGUID(0)
  , _world (world)
  , _context(context)
{

  QSettings settings;
  _unload_interval = settings.value("unload_interval", 30).toInt();
  _unload_dist = settings.value("unload_dist", 5).toInt();
  _loading_radius = settings.value("loading_radius", 2).toInt();

  if (create_empty)
  {
    mphd = {};

    mHasAGlobalWMO = false;
    mBigAlpha = true;
    _sort_models_by_size_class = true;
    changed = false;

    for (int j = 0; j < 64; ++j)
    {
      for (int i = 0; i < 64; ++i)
      {
        mTiles[j][i].tile = nullptr;
        mTiles[j][i].onDisc = false;
        mTiles[j][i].flags = 0;
      }
    }

    return;
  }

  std::stringstream filename;
  filename << "World\\Maps\\" << basename << "\\" << basename << ".wdt";

  BlizzardArchive::ClientFile theFile(filename.str(), Noggit::Application::NoggitApplication::instance()->clientData());

  uint32_t fourcc;
  uint32_t size;

  // - MVER ----------------------------------------------

  uint32_t version;

  theFile.read(&fourcc, 4);
  theFile.read(&size, 4);
  theFile.read(&version, 4);

  //! \todo find the correct version of WDT files.
  assert(fourcc == 'MVER' && version == 18);

  // - MHDR ----------------------------------------------

  theFile.read(&fourcc, 4);
  theFile.read(&size, 4);

  assert(fourcc == 'MPHD');

  // Retail ≥8.1: MPHD is flags, lgtFileDataID, occ, … (wowdev WDT). WotLK: flags, something, …
  if (client_uses_modern_wdt_mphd() && size >= 32)
  {
    std::uint32_t w[8];
    theFile.read (w, sizeof (w));
    mphd.flags = w[0];
    mphd.something = 0;
    mphd.lgtFileDataID = w[1];
    mphd.occFileDataID = w[2];
    mphd.fogsFileDataID = w[3];
    mphd.mpvFileDataID = w[4];
    mphd.texFileDataID = w[5];
    mphd.wdlFileDataID = w[6];
    if (size > sizeof (w))
      theFile.seekRelative (size - sizeof (w));
  }
  else
  {
    std::memset (&mphd, 0, sizeof (mphd));
    std::uint32_t const to_read = std::min (size, static_cast<std::uint32_t>(sizeof (MPHD)));
    if (to_read)
      theFile.read (reinterpret_cast<char*>(&mphd), to_read);
    if (size > to_read)
      theFile.seekRelative (size - to_read);
  }

  mHasAGlobalWMO = mphd.flags & FLAG_GLOBAL_OBJECT;
  mBigAlpha = mphd.flags & FLAG_BIG_ALPHA;
  _sort_models_by_size_class = mphd.flags & FLAG_DOODADS_SORT;

  if (!(mphd.flags & FLAG_SHADING))
  {
    mphd.flags |= FLAG_SHADING;
    changed = true;
  }

  // - MAIN ----------------------------------------------

  theFile.read(&fourcc, 4);
  theFile.seekRelative(4);

  assert(fourcc == 'MAIN');

  /// this is the theory. Sadly, we are also compiling on 64 bit machines with size_t being 8 byte, not 4. Therefore, we can't do the same thing, Blizzard does in its 32bit executable.
  //theFile.read( &(mTiles[0][0]), sizeof( 8 * 64 * 64 ) );

  // We could skip for WMO only maps
  for (int j = 0; j < 64; ++j)
  {
    for (int i = 0; i < 64; ++i)
    {
      theFile.read(&mTiles[j][i].flags, 4);
      theFile.seekRelative(4);

      mTiles[j][i].tile = nullptr;

      if (!(mTiles[j][i].flags & 1))
        continue;

      std::stringstream adt_filename;
      adt_filename << "World\\Maps\\" << basename << "\\" << basename << "_" << i << "_" << j << ".adt";

      mTiles[j][i].onDisc = Noggit::Application::NoggitApplication::instance()->clientData()->existsOnDisk(adt_filename.str());

			if (mTiles[j][i].onDisc)
			{
				mTiles[j][i].flags |= 1;
				changed = true;
			}
		}
	}

  if (!theFile.isEof() && mHasAGlobalWMO)
  {
    //! \note We actually don't load WMO only worlds, so we just stop reading here, k?
    //! \bug MODF reads wrong. The assertion fails every time. Somehow, it keeps being MWMO. Or are there two blocks?
    //! \nofuckingbug  on eof read returns just without doing sth to the var and some wdts have a MWMO without having a MODF so only checking for eof above is not enough

    // mHasAGlobalWMO = false;

    // - MWMO ----------------------------------------------

    theFile.read(&fourcc, 4);
    theFile.read(&size, 4);

    assert(fourcc == 'MWMO');

    globalWMOName = std::string(theFile.getPointer(), size);
    theFile.seekRelative(size);

    // - MODF ----------------------------------------------

    theFile.read(&fourcc, 4);
    theFile.read(&size, 4);

    assert(fourcc == 'MODF');

    theFile.read(&wmoEntry, sizeof(ENTRY_MODF));
    math::to_client(wmoEntry.pos);
  }

  // -----------------------------------------------------

  theFile.close();

  loadMinimapMD5translate();

  loadPointLightsFromLgtWdt(basename, _world);
}

void MapIndex::saveall (World* world)
{
  world->wait_for_all_tile_updates();

  saveMaxUID();

  for (MapTile* tile : loaded_tiles())
  {
    world->horizon.update_horizon_tile(tile);
    tile->saveTile(world);
    tile->changed = false;
  }

  savePointLightsToLgtWdt(basename, _world);
}

void MapIndex::save()
{
  std::stringstream filename;
  filename << "World\\Maps\\" << basename << "\\" << basename << ".wdt";

  //Log << "Saving WDT \"" << filename << "\"." << std::endl;

  util::sExtendableArray wdtFile;
  int curPos = 0;

  // MVER
  //  {
  wdtFile.Extend(8 + 0x4);
  SetChunkHeader(wdtFile, curPos, 'MVER', 4);

  // MVER data
  *(wdtFile.GetPointer<int>(8)) = 18;

  curPos += 8 + 0x4;
  //  }

  // MPHD
  //  {
  wdtFile.Extend(8);
  SetChunkHeader(wdtFile, curPos, 'MPHD', sizeof(MPHD));
  curPos += 8;

  mphd.flags = 0;
  mphd.something = 0;
  if (mHasAGlobalWMO)
      mphd.flags |= FLAG_GLOBAL_OBJECT;
  if (mBigAlpha)
      mphd.flags |= FLAG_BIG_ALPHA;
  if (_sort_models_by_size_class)
      mphd.flags |= FLAG_DOODADS_SORT;

  mphd.flags |= FLAG_SHADING;

  // Retail resolves `_lgt.wdt` only via MPHD.lgtFileDataID. Custom maps often had this at 0 because it was never
  // filled from listfile; the client then skips _lgt entirely (no point/spot lights in-game).
  if (client_uses_modern_wdt_mphd())
  {
    std::string const lgt_lookup = BlizzardArchive::ClientData::normalizeFilenameInternal (
      std::string ("World\\Maps\\") + basename + "\\" + basename + "_lgt.wdt");
    if (BlizzardArchive::ClientData* const cd = Noggit::Application::NoggitApplication::instance()->clientData())
    {
      if (BlizzardArchive::Listfile::Listfile const* const lf = cd->listfile())
      {
        std::uint32_t const lf_lgt = lf->getFileDataID (lgt_lookup);
        if (lf_lgt != 0u)
          mphd.lgtFileDataID = lf_lgt;
        else if (mphd.lgtFileDataID == 0u && _world && !_world->pointLights().empty())
        {
          LogError << "MPHD.lgtFileDataID is 0 but this map has point/spot lights. Add a listfile.csv line for "
                   << lgt_lookup
                   << " with the same FileDataID your patch/client uses for that file (e.g. Epsilon patch.json id), "
                      "then save the WDT again."
                   << std::endl;
        }
      }
    }
  }

  if (client_uses_modern_wdt_mphd())
  {
    std::uint32_t const pack[8] = {
      mphd.flags,
      mphd.lgtFileDataID,
      mphd.occFileDataID,
      mphd.fogsFileDataID,
      mphd.mpvFileDataID,
      mphd.texFileDataID,
      mphd.wdlFileDataID,
      0u, // pd4FileDataID
    };
    wdtFile.Insert (curPos, sizeof (pack), reinterpret_cast<char const*>(pack));
    curPos += sizeof (pack);
  }
  else
  {
    wdtFile.Insert (curPos, sizeof (MPHD), reinterpret_cast<char*>(&mphd));
    curPos += sizeof (MPHD);
  }

  // MAIN
  //  {
  wdtFile.Extend(8);
  SetChunkHeader(wdtFile, curPos, 'MAIN', 64 * 64 * 8);
  curPos += 8;

  for (int j = 0; j < 64; ++j)
  {
    for (int i = 0; i < 64; ++i)
    {
      wdtFile.Insert(curPos, 4, (char*)&mTiles[j][i].flags);
      wdtFile.Extend(4);
      curPos += 8;
    }
  }
  //  }

  if (mHasAGlobalWMO)
  {
    // MWMO
    //  {
    // the game requires the path to be zero terminated!
    if(globalWMOName[globalWMOName.size() - 1] != '\0')
    {
        globalWMOName += '\0';
    }
    wdtFile.Extend(8);
    SetChunkHeader(wdtFile, curPos, 'MWMO', static_cast<int>(globalWMOName.size()));
    curPos += 8;

    wdtFile.Insert(curPos, static_cast<unsigned long>(globalWMOName.size()), globalWMOName.data());
    curPos += static_cast<int>(globalWMOName.size());
    //  }

    // MODF
    //  {
    wdtFile.Extend(8);
    SetChunkHeader(wdtFile, curPos, 'MODF', sizeof(ENTRY_MODF));
    curPos += 8;

    auto entry = wmoEntry;
    math::to_server(entry.pos);
    wdtFile.Insert(curPos, sizeof(ENTRY_MODF), (char*)&entry);
    curPos += sizeof(ENTRY_MODF);
    //  }
  }

  BlizzardArchive::ClientFile f(filename.str(), Noggit::Application::NoggitApplication::instance()->clientData(),
                                BlizzardArchive::ClientFile::NEW_FILE);
  f.setBuffer(wdtFile.all_data());
  f.save();
  f.close();

  savePointLightsToLgtWdt(basename, _world);

  changed = false;
}

void MapIndex::enterTile(const TileIndex& tile)
{
  if (!hasTile(tile))
  {
    return;
  }

  int cx = static_cast<int>(tile.x);
  int cz = static_cast<int>(tile.z);

  for (int pz = std::max(cz - _loading_radius, 0); pz <= std::min(cz + _loading_radius, 63); ++pz)
  {
      for (int px = std::max(cx - _loading_radius, 0); px <= std::min(cx + _loading_radius, 63); ++px)
    {
      loadTile(TileIndex(px, pz));
    }
  }
}

void MapIndex::update_model_tile(const TileIndex& tile, model_update type, SceneObject* instance)
{
  MapTile* adt = loadTile(tile);

  if (!adt)
    return;

  adt->wait_until_loaded();
  adt->changed = true;

  if (type == model_update::add)
  {
    adt->add_model(instance);
  }
  else if(type == model_update::remove)
  {
    adt->remove_model(instance);
  }
}

void MapIndex::setChanged(const TileIndex& tile)
{
  MapTile* mTile = loadTile(tile);

  if (!!mTile)
  {
    mTile->changed = true;
  }
}

void MapIndex::setChanged(MapTile* tile)
{
  setChanged(tile->index);
}

void MapIndex::unsetChanged(const TileIndex& tile)
{
  // change the changed flag of the map tile
  if (hasTile(tile))
  {
    mTiles[tile.z][tile.x].tile->changed = false;
  }
}

bool MapIndex::has_unsaved_changes(const TileIndex& tile) const
{
  return (tileLoaded(tile) ? getTile(tile)->changed.load() : false);
}

void MapIndex::setFlag(bool to, glm::vec3 const& pos, uint32_t flag)
{
  TileIndex tile(pos);

  if (tileLoaded(tile))
  {
    setChanged(tile);

    int cx = (pos.x - tile.x * TILESIZE) / CHUNKSIZE;
    int cz = (pos.z - tile.z * TILESIZE) / CHUNKSIZE;

    MapChunk* chunk = getTile(tile)->getChunk(cx, cz);
    NOGGIT_CUR_ACTION->registerChunkFlagChange(chunk);
    chunk->setFlag(to, flag);
  }
}

MapTile* MapIndex::loadTile(const TileIndex& tile, bool reloading, bool load_models, bool load_textures)
{
  if (!hasTile(tile))
  {
    return nullptr;
  }

  if (tileLoaded(tile) || tileAwaitingLoading(tile))
  {
    return mTiles[tile.z][tile.x].tile.get();
  }

  std::stringstream filename;
  filename << "World\\Maps\\" << basename << "\\" << basename << "_" << tile.x << "_" << tile.z << ".adt";

  if (!Noggit::Application::NoggitApplication::instance()->clientData()->exists(filename.str()))
  {
    LogError << "The requested tile \"" << filename.str() << "\" does not exist! Oo" << std::endl;
    return nullptr;
  }

  mTiles[tile.z][tile.x].tile = std::make_unique<MapTile> (static_cast<int>(tile.x), static_cast<int>(tile.z), filename.str(),
     mBigAlpha, load_models, use_mclq_green_lava(), reloading, _world, _context, tile_mode::edit, load_textures);

  MapTile* adt = mTiles[tile.z][tile.x].tile.get();

  AsyncLoader::instance->queue_for_load(adt);

  if (LoadTraceEnabled())
  {
    LogDebug << "Queuing ADT tile " << tile.x << ", " << tile.z << " (\"" << filename.str() << "\")"
             << std::endl;
  }

  _n_loaded_tiles++;

  return adt;
}

void MapIndex::reloadTile(const TileIndex& tile)
{
  if (tileLoaded(tile))
  {
    mTiles[tile.z][tile.x].tile.reset();
    loadTile(tile, true);
  }
}

void MapIndex::unloadTiles(const TileIndex& tile)
{
  if (((clock() / CLOCKS_PER_SEC) - _last_unload_time) > _unload_interval)
  {
    // ensure _unload_dist is always bigger than loading dist
    if (_unload_dist <= _loading_radius)
    {
        _unload_dist = _loading_radius + 1;
        QSettings settings;
        settings.setValue("unload_dist", _unload_dist);
        settings.sync();
    }

    for (MapTile* adt : loaded_tiles())
    {
      if (tile.dist(adt->index) > _unload_dist)
      {
        //Only unload adts not marked to save
        if (!adt->changed.load())
        {
          unloadTile(adt->index);
        }
      }
    }

    _last_unload_time = clock() / CLOCKS_PER_SEC;
  }
}

void MapIndex::unloadTile(const TileIndex& tile)
{
  // unloads a tile with given cords
  if (tileLoaded(tile))
  {
    // either log before or don't use a reference for the tile/make a copy
    // otherwise it can be deleted before the log because it comes from the adt itself (see unloadTiles)
    Log << "Unloading Tile " << tile.x << "-" << tile.z << std::endl;

    AsyncLoader::instance->ensure_deletable(mTiles[tile.z][tile.x].tile.get());
    mTiles[tile.z][tile.x].tile.reset();
    _n_loaded_tiles--;
  }
}

void MapIndex::markOnDisc(const TileIndex& tile, bool mto)
{
  if(tile.is_valid())
  {
    mTiles[tile.z][tile.x].onDisc = mto;
  }
}

bool MapIndex::isTileExternal(const TileIndex& tile) const
{
  // is onDisc
  return tile.is_valid() && mTiles[tile.z][tile.x].onDisc;
}

void MapIndex::saveTile(const TileIndex& tile, World* world, bool save_unloaded)
{
  world->wait_for_all_tile_updates();

	// save given tile
	if (save_unloaded)
  {
    auto filepath = std::filesystem::path (Noggit::Project::CurrentProject::get()->ProjectPath)
                    / BlizzardArchive::ClientData::normalizeFilenameInternal (mTiles[tile.z][tile.x].tile->file_key().filepath());

    QFile file(filepath.string().c_str());
    file.open(QIODevice::WriteOnly);

    mTiles[tile.z][tile.x].tile->initEmptyChunks();
    mTiles[tile.z][tile.x].tile->saveTile(world);
    savePointLightsToLgtWdt(basename, _world);
    return;
  }

	if (tileLoaded(tile))
	{
    saveMaxUID();
    world->horizon.update_horizon_tile(mTiles[tile.z][tile.x].tile.get());
		mTiles[tile.z][tile.x].tile->saveTile(world);
	}

  savePointLightsToLgtWdt(basename, _world);
}

void MapIndex::saveChanged (World* world, bool save_unloaded)
{
  world->wait_for_all_tile_updates();

  if (changed)
  {
    save();
  }

  if (!save_unloaded)
  {
    saveMaxUID();
  }
  else
  {
    for (int i = 0; i < 64; ++i)
    {
      for (int j = 0; j < 64; ++j)
      {
        if (!(mTiles[i][j].tile && mTiles[i][j].tile->changed.load()))
        {
          continue;
        }

        auto filepath = std::filesystem::path (Noggit::Project::CurrentProject::get()->ProjectPath)
                        / BlizzardArchive::ClientData::normalizeFilenameInternal (mTiles[i][j].tile->file_key().filepath());

        if (mTiles[i][j].flags & 0x1)
        {
          QFile file(filepath.string().c_str());
          file.open(QIODevice::WriteOnly);

          mTiles[i][j].tile->initEmptyChunks();
          mTiles[i][j].tile->saveTile(world);
          mTiles[i][j].tile->changed = false;
        }
        else
        {
          QFile file(filepath.string().c_str());
          file.remove();
        }
      }
    }
    savePointLightsToLgtWdt(basename, _world);
    return;
  }

  for (MapTile* tile : loaded_tiles())
  {
    if (tile->changed.load())
    {
      world->horizon.update_horizon_tile(tile);
      tile->saveTile(world);
      tile->changed = false;
    }
  }

  savePointLightsToLgtWdt(basename, _world);
}

bool MapIndex::hasAGlobalWMO() const
{
  return mHasAGlobalWMO;
}


bool MapIndex::hasTile(const TileIndex& tile) const
{
  return tile.is_valid() && (mTiles[tile.z][tile.x].flags & 1);
}

bool MapIndex::tileAwaitingLoading(const TileIndex& tile) const
{
  return hasTile(tile) && mTiles[tile.z][tile.x].tile && !mTiles[tile.z][tile.x].tile->finishedLoading();
}

bool MapIndex::tileLoaded(const TileIndex& tile) const
{
  return hasTile(tile) && mTiles[tile.z][tile.x].tile && mTiles[tile.z][tile.x].tile->finishedLoading();
}

MapTile* MapIndex::getTile(const TileIndex& tile) const
{
  return (tile.is_valid() ? mTiles[tile.z][tile.x].tile.get() : nullptr);
}

MapTile* MapIndex::getTileAbove(MapTile* tile) const
{
  TileIndex above(tile->index.x, tile->index.z - 1);
  if (tile->index.z == 0 || (!tileLoaded(above) && !tileAwaitingLoading(above)))
  {
    return nullptr;
  }

  MapTile* tile_above = mTiles[tile->index.z - 1][tile->index.x].tile.get();
  tile_above->wait_until_loaded();

  return tile_above;
}

MapTile* MapIndex::getTileLeft(MapTile* tile) const
{
  TileIndex left(tile->index.x - 1, tile->index.z);
  if (tile->index.x == 0 || (!tileLoaded(left) && !tileAwaitingLoading(left)))
  {
    return nullptr;
  }

  MapTile* tile_left = mTiles[tile->index.z][tile->index.x - 1].tile.get();
  tile_left->wait_until_loaded();

  return tile_left;
}

uint32_t MapIndex::getFlag(const TileIndex& tile) const
{
  return (tile.is_valid() ? mTiles[tile.z][tile.x].flags : 0);
}

void MapIndex::convert_alphamap(bool to_big_alpha)
{
  mBigAlpha = to_big_alpha;
  if (to_big_alpha)
  {
    mphd.flags |= 4;
  }
  else
  {
    mphd.flags &= 0xFFFFFFFB;
  }
}

bool MapIndex::hasBigAlpha() const
{
  return mBigAlpha;
}

void MapIndex::setBigAlpha(bool state)
{
  mBigAlpha = state;
}

unsigned MapIndex::getNLoadedTiles() const
{
  return _n_loaded_tiles;
}

bool MapIndex::sort_models_by_size_class() const
{
  return _sort_models_by_size_class;
}

void MapIndex::set_sort_models_by_size_class(bool state)
{
  _sort_models_by_size_class = state;
}


uint32_t MapIndex::getHighestGUIDFromFile(const std::string& pFilename) const
{
	uint32_t highGUID = 0;

    BlizzardArchive::ClientFile theFile(pFilename, Noggit::Application::NoggitApplication::instance()->clientData());
    if (theFile.isEof())
    {
      return highGUID;
    }

    uint32_t fourcc;
    uint32_t size;

    MHDR Header;

    // - MVER ----------------------------------------------

    uint32_t version;

    theFile.read(&fourcc, 4);
    theFile.seekRelative(4);
    theFile.read(&version, 4);

    assert(fourcc == 'MVER' && version == 18);

    // - MHDR ----------------------------------------------

    theFile.read(&fourcc, 4);
    theFile.seekRelative(4);

    assert(fourcc == 'MHDR');

    theFile.read(&Header, sizeof(MHDR));

    // - MDDF ----------------------------------------------

    theFile.seek(Header.mddf + 0x14);
    theFile.read(&fourcc, 4);
    theFile.read(&size, 4);

    assert(fourcc == 'MDDF');

    ENTRY_MDDF const* mddf_ptr = reinterpret_cast<ENTRY_MDDF const*>(theFile.getPointer());
    for (unsigned int i = 0; i < size / sizeof(ENTRY_MDDF); ++i)
    {
        highGUID = std::max(highGUID, mddf_ptr[i].uniqueID);
    }

    // - MODF ----------------------------------------------

    theFile.seek(Header.modf + 0x14);
    theFile.read(&fourcc, 4);
    theFile.read(&size, 4);

    assert(fourcc == 'MODF');

    ENTRY_MODF const* modf_ptr = reinterpret_cast<ENTRY_MODF const*>(theFile.getPointer());
    for (unsigned int i = 0; i < size / sizeof(ENTRY_MODF); ++i)
    {
        highGUID = std::max(highGUID, modf_ptr[i].uniqueID);
    }
    theFile.close();

    return highGUID;
}

// reloadable settings
void MapIndex::setLoadingRadius(int value)
{
  if (value < _unload_dist)
    _loading_radius = value;
}

void MapIndex::setUnloadDistance(int value)
{
  if (value > _loading_radius)
    _unload_dist = value;
}

void MapIndex::setUnloadInterval(int value)
{
  _unload_interval = value;
}

uint32_t MapIndex::newGUID()
{
  std::unique_lock<std::mutex> lock (_mutex);

#ifdef USE_MYSQL_UID_STORAGE
  QSettings settings;

  if (settings.value ("project/mysql/enabled", false).toBool())
  {
    mysql::updateUIDinDB(_map_id, highestGUID + 1); // update the highest uid in db, note that if the user don't save these uid won't be used (not really a problem tho) 
  }
#endif
  return ++highestGUID;
}

uid_fix_status MapIndex::fixUIDs (World* world, bool cancel_on_model_loading_error)
{
  // clear all selection groups since UIDs will change.
  // TODO : update them instead.
    _world->clear_selection_groups();

  // pre-cond: mTiles[z][x].flags are set

  // unload any previously loaded tile, although there shouldn't be as
  // the fix is executed before loading the map
  for (int z = 0; z < 64; ++z)
  {
    for (int x = 0; x < 64; ++x)
    {
      if (mTiles[z][x].tile)
      {
        MapTile* tile = mTiles[z][x].tile.get();

        // don't unload half loaded tiles
        tile->wait_until_loaded();

        unloadTile(tile->index);
      }
    }
  }

  _uid_fix_all_in_progress = true;

  auto models = std::make_unique<std::forward_list<ModelInstance>>();
  auto wmos = std::make_unique<std::forward_list<WMOInstance>>();

  for (int z = 0; z < 64; ++z)
  {
    for (int x = 0; x < 64; ++x)
    {
      if (!(mTiles[z][x].flags & 1))
      {
        continue;
      }

      std::stringstream filename;
      filename << "World\\Maps\\" << basename << "\\" << basename << "_" << x << "_" << z << ".adt";
      BlizzardArchive::ClientFile file(filename.str(), Noggit::Application::NoggitApplication::instance()->clientData());

      if (file.isEof())
      {
        continue;
      }

      std::array<glm::vec3, 2> tileExtents;
      tileExtents[0] = { x*TILESIZE, 0, z*TILESIZE };
      tileExtents[1] = { (x+1)*TILESIZE, 0, (z+1)*TILESIZE };
      misc::minmax(&tileExtents[0], &tileExtents[1]);

      std::forward_list<ENTRY_MDDF> modelEntries;
      std::forward_list<ENTRY_MODF> wmoEntries;
      std::vector<std::string> modelFilenames;
      std::vector<std::string> wmoFilenames;

      uint32_t fourcc;
      uint32_t size;

      MHDR Header;

      // - MVER ----------------------------------------------
      uint32_t version;
      file.read(&fourcc, 4);
      file.seekRelative(4);
      file.read(&version, 4);
      assert(fourcc == 'MVER' && version == 18);

      // - MHDR ----------------------------------------------
      file.read(&fourcc, 4);
      file.seekRelative(4);
      assert(fourcc == 'MHDR');
      file.read(&Header, sizeof(MHDR));

      // - MDDF ----------------------------------------------
      file.seek(Header.mddf + 0x14);
      file.read(&fourcc, 4);
      file.read(&size, 4);
      assert(fourcc == 'MDDF');

      ENTRY_MDDF const* mddf_ptr = reinterpret_cast<ENTRY_MDDF const*>(file.getPointer());

      for (unsigned int i = 0; i < size / sizeof(ENTRY_MDDF); ++i)
      {
        bool add = true;
        ENTRY_MDDF const& mddf = mddf_ptr[i];

        if (!misc::pointInside({ mddf.pos[0], 0, mddf.pos[2] }, tileExtents))
        {
          continue;
        }

        // check for duplicates
        for (ENTRY_MDDF& entry : modelEntries)
        {
          if ( mddf.nameID == entry.nameID
            && misc::float_equals(mddf.pos[0], entry.pos[0])
            && misc::float_equals(mddf.pos[1], entry.pos[1])
            && misc::float_equals(mddf.pos[2], entry.pos[2])
            && misc::float_equals(mddf.rot[0], entry.rot[0])
            && misc::float_equals(mddf.rot[1], entry.rot[1])
            && misc::float_equals(mddf.rot[2], entry.rot[2])
            && mddf.scale == entry.scale
            )
          {
            add = false;
            break;
          }
        }

        if (add)
        {
          modelEntries.emplace_front(mddf);
        }
      }

      // - MODF ----------------------------------------------
      file.seek(Header.modf + 0x14);
      file.read(&fourcc, 4);
      file.read(&size, 4);
      assert(fourcc == 'MODF');

      ENTRY_MODF const* modf_ptr = reinterpret_cast<ENTRY_MODF const*>(file.getPointer());

      for (unsigned int i = 0; i < size / sizeof(ENTRY_MODF); ++i)
      {
        bool add = true;
        ENTRY_MODF const& modf = modf_ptr[i];

        if (!misc::pointInside({ modf.pos[0], 0, modf.pos[2] }, tileExtents))
        {
          continue;
        }

        // check for duplicates
        for (ENTRY_MODF& entry : wmoEntries)
        {
          if (modf.nameID == entry.nameID
            && misc::float_equals(modf.pos[0], entry.pos[0])
            && misc::float_equals(modf.pos[1], entry.pos[1])
            && misc::float_equals(modf.pos[2], entry.pos[2])
            && misc::float_equals(modf.rot[0], entry.rot[0])
            && misc::float_equals(modf.rot[1], entry.rot[1])
            && misc::float_equals(modf.rot[2], entry.rot[2])
            )
          {
            add = false;
            break;
          }
        }

        if (add)
        {
          wmoEntries.emplace_front(modf);
        }
      }

      // - MMDX ----------------------------------------------
      file.seek(Header.mmdx + 0x14);
      file.read(&fourcc, 4);
      file.read(&size, 4);
      assert(fourcc == 'MMDX');

      {
        char const* lCurPos = reinterpret_cast<char const*>(file.getPointer());
        char const* lEnd = lCurPos + size;

        while (lCurPos < lEnd)
        {
          modelFilenames.push_back(std::string(lCurPos));
          lCurPos += strlen(lCurPos) + 1;
        }
      }

      // - MWMO ----------------------------------------------
      file.seek(Header.mwmo + 0x14);
      file.read(&fourcc, 4);
      file.read(&size, 4);
      assert(fourcc == 'MWMO');

      {
        char const* lCurPos = reinterpret_cast<char const*>(file.getPointer());
        char const* lEnd = lCurPos + size;

        while (lCurPos < lEnd)
        {
          wmoFilenames.push_back(std::string(lCurPos));
          lCurPos += strlen(lCurPos) + 1;
        }
      }

      file.close();

      for (ENTRY_MDDF& entry : modelEntries)
      {
        models->emplace_front(modelFilenames[entry.nameID], &entry, _context);
      }
      for (ENTRY_MODF& entry : wmoEntries)
      {
        wmos->emplace_front(wmoFilenames[entry.nameID], &entry, _context);
      }
    }
  }

  // set all uids
  // for each tile save the m2/wmo present inside
  highestGUID = 0;

  auto uids_per_tile = std::make_unique<std::map<std::size_t, std::map<std::size_t, std::forward_list<std::uint32_t>>>>();

  bool loading_error = false;

  for (ModelInstance& instance : *models)
  {
    instance.uid = highestGUID++;
    instance.model->wait_until_loaded();
    instance.recalcExtents();

    loading_error |= instance.model->loading_failed();

    // to avoid going outside of bound
    std::size_t sx = std::max((std::size_t)(instance.getExtents()[0].x / TILESIZE), (std::size_t)0);
    std::size_t sz = std::max((std::size_t)(instance.getExtents()[0].z / TILESIZE), (std::size_t)0);
    std::size_t ex = std::min((std::size_t)(instance.getExtents()[1].x / TILESIZE), (std::size_t)63);
    std::size_t ez = std::min((std::size_t)(instance.getExtents()[1].z / TILESIZE), (std::size_t)63);

    auto const real_uid (world->add_model_instance (std::move(instance), false, false));

    for (std::size_t z = sz; z <= ez; ++z)
    {
      auto& row_map = (*uids_per_tile)[z];
      for (std::size_t x = sx; x <= ex; ++x)
      {
          auto& uid_list = row_map[x];
          uid_list.emplace_front(real_uid);
      }
    }
  }

  models.reset();

  for (WMOInstance& instance : *wmos)
  {
    instance.uid = highestGUID++;
    instance.wmo->wait_until_loaded();
    instance.recalcExtents();
    // no need to check if the loading is finished since the extents are stored inside the adt
    // to avoid going outside of bound
    std::size_t sx = std::max((std::size_t)(instance.getExtents()[0].x / TILESIZE), (std::size_t)0);
    std::size_t sz = std::max((std::size_t)(instance.getExtents()[0].z / TILESIZE), (std::size_t)0);
    std::size_t ex = std::min((std::size_t)(instance.getExtents()[1].x / TILESIZE), (std::size_t)63);
    std::size_t ez = std::min((std::size_t)(instance.getExtents()[1].z / TILESIZE), (std::size_t)63);

    auto const real_uid (world->add_wmo_instance (std::move(instance), false, false));

    for (std::size_t z = sz; z <= ez; ++z)
    {
      auto& row_map = (*uids_per_tile)[z];
      for (std::size_t x = sx; x <= ex; ++x)
      {
        auto& uid_list = row_map[x];
        uid_list.emplace_front(real_uid);
      }
    }
  }

  wmos.reset();

  if (cancel_on_model_loading_error && loading_error)
  {
    return uid_fix_status::failed;
  }

  // load each tile without the models and
  // save them with the models with the new uids
  for (int z = 0; z < 64; ++z)
  {
    for (int x = 0; x < 64; ++x)
    {
      if (!(mTiles[z][x].flags & 1))
      {
        continue;
      }

      // load even the tiles without models in case there are old ones
      // that shouldn't be there to avoid creating new duplicates

      std::stringstream filename;
      filename << "World\\Maps\\" << basename << "\\" << basename << "_" << x << "_" << z << ".adt";

      // load the tile without the models
      MapTile tile(x, z, filename.str(), mBigAlpha, false, use_mclq_green_lava(), false, world, _context, tile_mode::uid_fix_all);
      tile.finishLoading();

      // add the uids to the tile to be able to save the models
      // which have been loaded in world earlier
      for (std::uint32_t uid : (*uids_per_tile)[z][x])
      {
        tile.add_model(uid);
      }

      tile.saveTile(world);
    }
  }

  // override the db highest uid if used
  saveMaxUID();

  _uid_fix_all_in_progress = false;

  // force instances unloading
  world->unload_every_model_and_wmo_instance();

  return loading_error ? uid_fix_status::done_with_errors : uid_fix_status::done;
}

void MapIndex::searchMaxUID()
{
  for (int z = 0; z < 64; ++z)
  {
    for (int x = 0; x < 64; ++x)
    {
      if (!(mTiles[z][x].flags & 1))
      {
        continue;
      }

      std::stringstream filename;
      filename << "World\\Maps\\" << basename << "\\" << basename << "_" << x << "_" << z << ".adt";
      highestGUID = std::max(highestGUID, getHighestGUIDFromFile(filename.str()));
    }
  }

  saveMaxUID();
}

void MapIndex::saveMaxUID()
{
#ifdef USE_MYSQL_UID_STORAGE
  QSettings settings;

  if (settings.value ("project/mysql/enabled", false).toBool())
  {
    if (mysql::hasMaxUIDStoredDB(_map_id))
    {
	    mysql::updateUIDinDB(_map_id, highestGUID);
    }
    else
    {
	    mysql::insertUIDinDB(_map_id, highestGUID);
    }
  }
#endif
  // save the max UID on the disk (always save to sync with the db if used
  uid_storage::saveMaxUID (_map_id, highestGUID);
}

void MapIndex::loadMaxUID()
{
  highestGUID = uid_storage::getMaxUID (_map_id);
#ifdef USE_MYSQL_UID_STORAGE
  QSettings settings;

  if (settings.value ("project/mysql/enabled", false).toBool())
  {
    highestGUID = std::max(mysql::getGUIDFromDB(_map_id), highestGUID);
    // save to make sure the db and disk uid are synced
    saveMaxUID();
  }
#endif
}

void MapIndex::loadMinimapMD5translate()
{
  auto& minimap_md5translate = Noggit::Application::NoggitApplication::instance()->clientData()->_minimap_md5translate;

  // already loaded.
  if (minimap_md5translate.empty())
    return;

  if (!Noggit::Application::NoggitApplication::instance()->clientData()->exists("textures/minimap/md5translate.trs"))
  {
    LogError << "md5translate.trs was not found. "
                "Noggit will generate a new one in the project directory on minimap save." << std::endl;
    return;
  }

  BlizzardArchive::ClientFile md5trs_file("textures/minimap/md5translate.trs", Noggit::Application::NoggitApplication::instance()->clientData());

  size_t size = md5trs_file.getSize();
  void* buffer_raw = std::malloc(size);
  md5trs_file.read(buffer_raw, size);

  QByteArray md5trs_bytes(static_cast<char*>(buffer_raw), static_cast<int>(size));

  QTextStream md5trs_stream(md5trs_bytes, QIODevice::ReadOnly);

  QString cur_dir = "";
  while (!md5trs_stream.atEnd())
  {
    QString line = md5trs_stream.readLine();

    if (!line.length())
    {
      continue;
    }

    if (line.startsWith("dir: ", Qt::CaseInsensitive))
    {
      QStringList dir_line_split = line.split(" ");
      cur_dir = dir_line_split[1];
      continue;
    }

    QStringList line_split = line.split(QRegExp("[\t]"));

    if (line_split.length() < 2)
    {
        std::string text = "Failed to read md5translate.trs.\nLine \"" + line.toStdString() + "\n has no tab spacing. Spacing must be only a tab character and not spaces.";
        LogError << text << std::endl;
        throw  std::logic_error(text);
    }

    if (cur_dir.length())
    {
      minimap_md5translate[cur_dir.toStdString()][line_split[0].toStdString()] = line_split[1].toStdString();
    }

  }

}

void MapIndex::saveMinimapMD5translate()
{
  QString str = QString(Noggit::Project::CurrentProject::get()->ProjectPath.c_str());
  if (!(str.endsWith('\\') || str.endsWith('/')))
  {
    str += "/";
  }

  QString filepath = str + "/textures/minimap/md5translate.trs";

  QFile file = QFile(filepath);
  if (file.open(QIODevice::WriteOnly | QIODevice::Text | QFile::Truncate))
  {
    QTextStream out(&file);

    auto const& minimap_md5translate = Noggit::Application::NoggitApplication::instance()->clientData()->_minimap_md5translate;

    for (auto it = minimap_md5translate.begin(); it != minimap_md5translate.end(); ++it)
    {
      out << "dir: " << it->first.c_str() << "\n"; // save dir

      for (auto it_ = it->second.begin(); it_ != it->second.end(); ++it_)
      {
        out << it_->first.c_str() << "\t" << it_->second.c_str() << "\n";
      }
    }

    file.close();
  }
  else
  {
    LogError << "Failed saving md5translate.trs. File can't be opened." << std::endl;
  }




}

void MapIndex::addTile(const TileIndex& tile)
{
  std::stringstream filename;
  filename << "World\\Maps\\" << basename << "\\" << basename << "_" << tile.x << "_" << tile.z << ".adt";

  mTiles[tile.z][tile.x].tile = std::make_unique<MapTile> (static_cast<int>(tile.x), static_cast<int>(tile.z), filename.str(),
      mBigAlpha, true, use_mclq_green_lava(), false, _world, _context);

  mTiles[tile.z][tile.x].flags |= 0x1;
  mTiles[tile.z][tile.x].tile->changed = true;

  _world->horizon.update_horizon_tile(mTiles[tile.z][tile.x].tile.get());

  changed = true;
}

void MapIndex::removeTile(const TileIndex &tile)
{
  mTiles[tile.z][tile.x].flags &= ~0x1;

  std::stringstream filename;
  filename << "World\\Maps\\" << basename << "\\" << basename << "_" << tile.x << "_" << tile.z << ".adt";
  mTiles[tile.z][tile.x].tile = std::make_unique<MapTile> (static_cast<int>(tile.x), static_cast<int>(tile.z), filename.str(),
     mBigAlpha, true, use_mclq_green_lava(), false, _world, _context);

  mTiles[tile.z][tile.x].tile->changed = true;
  mTiles[tile.z][tile.x].onDisc = false;

  _world->horizon.remove_horizon_tile(tile.z, tile.x);

  changed = true;
}

void MapIndex::addGlobalWmo(std::string path, ENTRY_MODF entry)
{
    mHasAGlobalWMO = true;
    globalWMOName = std::move(path);
    wmoEntry = std::move(entry);
}

void MapIndex::removeGlobalWmo()
{
    mHasAGlobalWMO = false;
    globalWMOName.clear();
    wmoEntry = {};
}

unsigned MapIndex::getNumExistingTiles()
{
  if (_n_existing_tiles >= 0)
    return _n_existing_tiles;

  _n_existing_tiles = 0;
  for (int i = 0; i < 4096; ++i)
  {
    TileIndex index(i / 64, i % 64);

    if (hasTile(index))
    {
      _n_existing_tiles++;
    }
  }

  return _n_existing_tiles;
}

// todo: find out how wow choose to use the green lava in outland
bool MapIndex::use_mclq_green_lava() const
{
  return _map_id == 530;
}

bool MapIndex::uid_fix_all_in_progress() const
{
  return _uid_fix_all_in_progress;
}

void MapIndex::set_basename(const std::string &pBasename)
{
  basename = pBasename;

  for (int z = 0; z < 64; ++z)
  {
    for (int x = 0; x < 64; ++x)
    {
      if (!mTiles[z][x].tile)
      {
        continue;
      }

      std::stringstream filename;
      filename << "World\\Maps\\" << basename << "\\" << basename << "_" << x << "_" << z << ".adt";

      mTiles[z][x].tile->setFilename(filename.str());
    }
  }
}

void MapIndex::create_empty_wdl() const
{
    // for new map creation, creates a new WDL with all heights as 0
    std::stringstream filename;
    filename << "World\\Maps\\" << basename << "\\" << basename << ".wdl"; // mapIndex.basename ? 
    //Log << "Saving WDL \"" << filename << "\"." << std::endl;

    util::sExtendableArray wdlFile;
    int curPos = 0;

    // MVER
    //  {
    wdlFile.Extend(8 + 0x4);
    SetChunkHeader(wdlFile, curPos, 'MVER', 4);

    // MVER data
    *(wdlFile.GetPointer<int>(8)) = 18; // write version 18
    curPos += 8 + 0x4;
    //  }

    // MWMO
    //  {
    wdlFile.Extend(8);
    SetChunkHeader(wdlFile, curPos, 'MWMO', 0);

    curPos += 8;
    //  }

    // MWID
    //  {
    wdlFile.Extend(8);
    SetChunkHeader(wdlFile, curPos, 'MWID', 0);

    curPos += 8;
    //  }

    // MODF
    //  {
    wdlFile.Extend(8);
    SetChunkHeader(wdlFile, curPos, 'MODF', 0);

    curPos += 8;
    //  }

    uint32_t* mare_offsets = new uint32_t[4096]();
    // uint32_t mare_offsets[4096] = { 0 }; // [64][64];
    // MAOF
    //  {
    wdlFile.Extend(8);
    SetChunkHeader(wdlFile, curPos, 'MAOF', 64 * 64 * 4);
    curPos += 8;

    uint32_t mareoffset = curPos + 64 * 64 * 4;

    for (int y = 0; y < 64; ++y)
    {
        for (int x = 0; x < 64; ++x)
        {
            TileIndex index(x, y);

            bool has_tile = hasTile(index);

            // if (tile_exists)
            if (has_tile) // TODO check if tile exists
            {
                // write offset in MAOF entry
                wdlFile.Insert(curPos, 4, (char*)&mareoffset);
                mare_offsets[y * 64 + x] = mareoffset;
                mareoffset += 1138; // mare + maho
            }
            else
                wdlFile.Extend(4);
            curPos += 4;

        }
    }

    for (int i = 0; i < 4096; ++i)
    {
        uint32_t offset = mare_offsets[i];
        if (!offset)
            continue;

        // MARE
        //  {
        wdlFile.Extend(8);
        SetChunkHeader(wdlFile, curPos, 'MARE', (2 * (17 * 17)) + (2 * (16 * 16))); // outer heights+inner heights
        curPos += 8;

        // write inner and outer heights
        wdlFile.Extend((2 * (17 * 17)) + (2 * (16 * 16)));
        curPos += (2 * (17 * 17)) + (2 * (16 * 16));
        //  }

        // MAHO (maparea holes)
        //  {
        wdlFile.Extend(8);
        SetChunkHeader(wdlFile, curPos, 'MAHO', 2 * 16); // 1 hole mask for each chunk
        curPos += 8;

        wdlFile.Extend(32);
        curPos += 32;
    }
    delete[] mare_offsets;

    BlizzardArchive::ClientFile f(filename.str(), Noggit::Application::NoggitApplication::instance()->clientData(),
    BlizzardArchive::ClientFile::NEW_FILE);
    f.setBuffer(wdlFile.all_data());
    f.save();
    f.close();
}

MapTileEntry::~MapTileEntry()
{
}

MapTileEntry::MapTileEntry()
  : flags(0)
  , tile(nullptr)
  , onDisc(false)
{
}
