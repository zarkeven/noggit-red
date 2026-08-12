// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/integrations/MapLightsJsonInjector.hpp>
#include <noggit/format/ChunkReader.hpp>
#include <noggit/Log.h>

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace
{
  constexpr int k_wdt_lgt_fdid_offset = 28;

  std::string to_lower_copy (std::string s)
  {
    for (char& c : s)
      if (c >= 'A' && c <= 'Z')
        c = static_cast<char>(c - 'A' + 'a');
    return s;
  }

  bool patch_wdt_lgt_file_data_id (std::vector<std::uint8_t>& buf, std::uint32_t lgt_fdid)
  {
    if (buf.size() < static_cast<std::size_t>(k_wdt_lgt_fdid_offset + 4))
      return false;

    std::uint32_t const mver = Noggit::Format::make_fourcc ('M', 'V', 'E', 'R');
    if (std::memcmp (buf.data(), &mver, 4) != 0)
      return false;

    std::uint32_t const mphd = Noggit::Format::make_fourcc ('M', 'P', 'H', 'D');
    if (buf.size() < 20 || std::memcmp (buf.data() + 12, &mphd, 4) != 0)
      return false;

    std::uint32_t mphd_size = 0;
    std::memcpy (&mphd_size, buf.data() + 16, 4);
    if (mphd_size < 8 + 4 + 4)
      return false;

    std::memcpy (buf.data() + k_wdt_lgt_fdid_offset, &lgt_fdid, 4);
    return true;
  }

  void normalize_manifest_path_inplace (std::string& p)
  {
    for (char& c : p)
    {
      if (c == '\\')
        c = '/';
      c = static_cast<char>(std::tolower (static_cast<unsigned char>(c)));
    }
  }

  bool paths_equal_lower (std::string const& a, std::string const& b)
  {
    if (a.size() != b.size())
      return false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
      char ca = static_cast<char>(std::tolower (static_cast<unsigned char>(a[i])));
      char cb = static_cast<char>(std::tolower (static_cast<unsigned char>(b[i])));
      if (ca == '\\' && cb == '/')
        continue;
      if (ca == '/' && cb == '\\')
        continue;
      if (ca != cb)
        return false;
    }
    return true;
  }

  std::optional<std::uint32_t> lookup_lgt_fdid_from_patch (
    std::filesystem::path const& patch_dir
  , std::string const& rel_lgt_norm)
  {
    std::filesystem::path const patch_json_path = patch_dir / "patch.json";
    if (!std::filesystem::is_regular_file (patch_json_path))
      return std::nullopt;

    QFile jf (QString::fromStdString (patch_json_path.string()));
    if (!jf.open (QIODevice::ReadOnly))
      return std::nullopt;

    QJsonParseError perr {};
    QJsonDocument const doc = QJsonDocument::fromJson (jf.readAll(), &perr);
    jf.close();
    if (perr.error != QJsonParseError::NoError || !doc.isObject())
      return std::nullopt;

    for (auto const& v : doc.object().value ("files").toArray())
    {
      if (!v.isObject())
        continue;
      QJsonObject o = v.toObject();
      std::string fn = o["file"].toString().toStdString();
      normalize_manifest_path_inplace (fn);
      if (paths_equal_lower (fn, rel_lgt_norm))
        return static_cast<std::uint32_t>(o["id"].toVariant().toULongLong());
    }
    return std::nullopt;
  }

  bool upsert_ngpl_in_adt (std::vector<std::uint8_t>& adt, std::uint8_t ngpl_cap_encoded)
  {
    std::uint32_t const ngpl_fourcc = Noggit::Format::make_fourcc ('N', 'G', 'P', 'L');
    std::size_t ngpl_pos = std::string::npos;
    std::size_t pos = 0;

    while (pos + 8 <= adt.size())
    {
      std::uint32_t fourcc = 0;
      std::uint32_t chsize = 0;
      std::memcpy (&fourcc, adt.data() + pos, 4);
      std::memcpy (&chsize, adt.data() + pos + 4, 4);
      if (fourcc == ngpl_fourcc)
        ngpl_pos = pos;
      pos += 8 + chsize;
    }

    if (ngpl_cap_encoded == 0)
    {
      if (ngpl_pos == std::string::npos)
        return true;
      std::uint32_t chsize = 0;
      std::memcpy (&chsize, adt.data() + ngpl_pos + 4, 4);
      adt.erase (adt.begin() + static_cast<std::ptrdiff_t>(ngpl_pos)
                , adt.begin() + static_cast<std::ptrdiff_t>(ngpl_pos + 8 + chsize));
      return true;
    }

    std::uint32_t const payload = static_cast<std::uint32_t>(ngpl_cap_encoded);
    if (ngpl_pos != std::string::npos)
    {
      std::uint32_t chsize = 0;
      std::memcpy (&chsize, adt.data() + ngpl_pos + 4, 4);
      if (chsize == 4 && ngpl_pos + 12 <= adt.size())
      {
        std::memcpy (adt.data() + ngpl_pos + 8, &payload, 4);
        return true;
      }
      adt.erase (adt.begin() + static_cast<std::ptrdiff_t>(ngpl_pos)
                , adt.begin() + static_cast<std::ptrdiff_t>(ngpl_pos + 8 + chsize));
    }

    std::array<std::uint8_t, 12> chunk {};
    std::memcpy (chunk.data(), &ngpl_fourcc, 4);
    std::uint32_t const size = 4;
    std::memcpy (chunk.data() + 4, &size, 4);
    std::memcpy (chunk.data() + 8, &payload, 4);
    adt.insert (adt.end(), chunk.begin(), chunk.end());
    return true;
  }
}

namespace Noggit::Integrations
{
  MapLightsJsonInjector::Result MapLightsJsonInjector::inject_ngpl_caps (
    MapLights::MapLightsManifest const& manifest
  , EpsilonExportConfig const& cfg
  , std::string const& map_basename)
  {
    Result result;
    if (manifest.adt_light_caps.empty())
    {
      result.success = true;
      result.message = "No custom NGPL caps in manifest.";
      return result;
    }

    namespace fs = std::filesystem;
    std::string const map_lower = to_lower_copy (map_basename);
    fs::path const map_out_dir = cfg.patches_path / cfg.patch_name / "world" / "maps" / map_lower;

    int updated = 0;
    for (auto const& cap : manifest.adt_light_caps)
    {
      std::ostringstream adt_name;
      adt_name << map_lower << "_" << static_cast<int>(cap.tile_x) << "_" << static_cast<int>(cap.tile_y) << ".adt";
      fs::path const adt_path = map_out_dir / adt_name.str();

      std::error_code ec;
      if (!fs::is_regular_file (adt_path, ec) || ec)
      {
        Log << "Map lights inject: ADT not in patch, skipping NGPL for "
            << adt_name.str() << std::endl;
        continue;
      }

      std::vector<std::uint8_t> buf;
      {
        std::ifstream in (adt_path, std::ios::binary);
        if (!in)
        {
          LogError << "Map lights inject: cannot read " << adt_path.string() << std::endl;
          continue;
        }
        in.seekg (0, std::ios::end);
        auto const sz = in.tellg();
        in.seekg (0, std::ios::beg);
        if (sz <= 0 || sz > 64 * 1024 * 1024)
          continue;
        buf.resize (static_cast<std::size_t>(sz));
        in.read (reinterpret_cast<char*>(buf.data()), sz);
        if (!in)
          continue;
      }

      if (!upsert_ngpl_in_adt (buf, cap.ngpl_cap_encoded))
        continue;

      std::ofstream out (adt_path, std::ios::binary | std::ios::trunc);
      if (!out)
      {
        LogError << "Map lights inject: cannot write " << adt_path.string() << std::endl;
        continue;
      }
      out.write (reinterpret_cast<char const*>(buf.data()), static_cast<std::streamsize>(buf.size()));
      ++updated;
    }

    result.success = true;
    result.message = "Injected NGPL on " + std::to_string (updated) + " ADT(s).";
    Log << "Map lights inject: " << result.message << std::endl;
    return result;
  }

  MapLightsJsonInjector::Result MapLightsJsonInjector::inject (
    EpsilonExportConfig const& cfg
  , std::filesystem::path const& project_path
  , std::string const& map_basename)
  {
    Result result;
    namespace fs = std::filesystem;

    fs::path const json_path = MapLights::manifest_path_for_map (project_path, map_basename);
    auto const manifest = MapLights::read_json (json_path);
    if (!manifest)
    {
      result.message = "Map lights JSON not found or invalid: " + json_path.string();
      LogError << result.message << std::endl;
      return result;
    }

    std::string const map_lower = to_lower_copy (map_basename);
    std::string const rel_lgt = std::string ("world/maps/") + map_lower + "/" + map_lower + "_lgt.wdt";
    std::string const rel_wdt = std::string ("world/maps/") + map_lower + "/" + map_lower + ".wdt";

    fs::path const patch_dir = cfg.patches_path / cfg.patch_name;
    fs::path const map_out_dir = patch_dir / "world" / "maps" / map_lower;

    std::error_code ec;
    fs::create_directories (map_out_dir, ec);
    if (ec)
    {
      result.message = "Failed to create patch directories: " + ec.message();
      LogError << result.message << std::endl;
      return result;
    }

    fs::path const out_lgt = map_out_dir / (map_lower + "_lgt.wdt");
    if (!MapLights::write_lgt_wdt (*manifest, out_lgt))
    {
      result.message = "Failed to write _lgt.wdt to patch.";
      return result;
    }

    std::optional<std::uint32_t> lgt_fdid = lookup_lgt_fdid_from_patch (patch_dir, rel_lgt);

    fs::path const src_wdt = project_path / "World" / "Maps" / map_basename / (map_basename + ".wdt");
    fs::path const patch_wdt = map_out_dir / (map_lower + ".wdt");

    if (fs::is_regular_file (src_wdt, ec) && !ec)
    {
      std::vector<std::uint8_t> wdt_buf;
      {
        std::ifstream in (src_wdt, std::ios::binary);
        if (in)
        {
          in.seekg (0, std::ios::end);
          auto const sz = in.tellg();
          in.seekg (0, std::ios::beg);
          if (sz > 0 && sz <= 100 * 1024 * 1024)
          {
            wdt_buf.resize (static_cast<std::size_t>(sz));
            in.read (reinterpret_cast<char*>(wdt_buf.data()), sz);
          }
        }
      }

      if (!wdt_buf.empty() && lgt_fdid && *lgt_fdid != 0)
      {
        if (patch_wdt_lgt_file_data_id (wdt_buf, *lgt_fdid))
        {
          std::ofstream out (patch_wdt, std::ios::binary | std::ios::trunc);
          if (out)
            out.write (reinterpret_cast<char const*>(wdt_buf.data()), static_cast<std::streamsize>(wdt_buf.size()));
        }
      }
      else if (!wdt_buf.empty() && fs::is_regular_file (patch_wdt, ec) && !ec && lgt_fdid && *lgt_fdid != 0)
      {
        std::ifstream in (patch_wdt, std::ios::binary);
        wdt_buf.clear();
        if (in)
        {
          in.seekg (0, std::ios::end);
          auto const sz = in.tellg();
          in.seekg (0, std::ios::beg);
          if (sz > 0 && sz <= 100 * 1024 * 1024)
          {
            wdt_buf.resize (static_cast<std::size_t>(sz));
            in.read (reinterpret_cast<char*>(wdt_buf.data()), sz);
          }
        }
        if (!wdt_buf.empty() && patch_wdt_lgt_file_data_id (wdt_buf, *lgt_fdid))
        {
          std::ofstream out (patch_wdt, std::ios::binary | std::ios::trunc);
          if (out)
            out.write (reinterpret_cast<char const*>(wdt_buf.data()), static_cast<std::streamsize>(wdt_buf.size()));
        }
      }
    }

    auto const ngpl_result = inject_ngpl_caps (*manifest, cfg, map_basename);

    result.success = true;
    result.message = std::string ("Injected _lgt.wdt from JSON");
    if (lgt_fdid)
      result.message += " (lgt fdid " + std::to_string (*lgt_fdid) + ")";
    result.message += ". " + ngpl_result.message;
    Log << "Map lights inject: " << result.message << std::endl;
    return result;
  }
}
