// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/integrations/EpsilonPatchExporter.hpp>
#include <noggit/integrations/MapLightsJsonInjector.hpp>
#include <noggit/map_lights/MapLightsManifest.hpp>
#include <noggit/format/ChunkReader.hpp>
#include <noggit/Log.h>

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSettings>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace
{
  constexpr int k_wdt_lgt_fdid_offset = 28; // after MVER (12) + MPHD header (8) + flags (4) + something (4)

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

    std::uint32_t const mver = Noggit::Format::make_fourcc('M', 'V', 'E', 'R');
    if (std::memcmp (buf.data(), &mver, 4) != 0)
      return false;

    std::uint32_t const mphd = Noggit::Format::make_fourcc('M', 'P', 'H', 'D');
    if (buf.size() < 20 || std::memcmp (buf.data() + 12, &mphd, 4) != 0)
      return false;

    std::uint32_t mphd_size = 0;
    std::memcpy (&mphd_size, buf.data() + 16, 4);
    if (mphd_size < 8 + 4 + 4)
      return false;

    std::memcpy (buf.data() + k_wdt_lgt_fdid_offset, &lgt_fdid, 4);
    return true;
  }

  bool paths_equal_lower (std::string const& a, std::string const& b)
  {
    if (a.size() != b.size())
      return false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
      char ca = static_cast<char>(std::tolower (static_cast<unsigned char>(a[i])));
      char cb = static_cast<char>(std::tolower (static_cast<unsigned char>(b[i])));
      if (ca != cb)
        return false;
      if (ca == '\\' && cb == '/')
        continue;
      if (ca == '/' && cb == '\\')
        continue;
    }
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

  int parse_mu_version (QString const& v)
  {
    if (!v.startsWith ("MU-", Qt::CaseInsensitive))
      return 0;
    bool ok = false;
    int n = v.mid (3).toInt (&ok);
    return ok ? n : 0;
  }
}

namespace Noggit::Integrations
{
  EpsilonPatchExporter& EpsilonPatchExporter::instance()
  {
    static EpsilonPatchExporter s;
    return s;
  }

  std::optional<EpsilonExportConfig> EpsilonPatchExporter::load_config_from_settings()
  {
    QSettings settings;
    if (!settings.value ("integrations/epsilon_enabled", false).toBool())
      return std::nullopt;

    QString const patches = settings.value ("integrations/epsilon_patches_path").toString().trimmed();
    QString const patch_name = settings.value ("integrations/epsilon_patch_name").toString().trimmed();
    if (patches.isEmpty() || patch_name.isEmpty())
      return std::nullopt;

    EpsilonExportConfig cfg;
    cfg.patches_path = std::filesystem::path (patches.toStdWString());
    cfg.patch_name = patch_name.toStdString();
    cfg.starting_fdid = static_cast<std::uint32_t>(
      std::clamp (settings.value ("integrations/epsilon_starting_fdid", 5'000'000).toInt(), 100'000, 2'147'483'647));
    cfg.wdt_fdid_override = static_cast<std::uint32_t>(
      std::max (0, settings.value ("integrations/epsilon_wdt_fdid", 0).toInt()));
    cfg.export_map_basename = settings.value ("integrations/epsilon_export_map_basename").toString().trimmed().toStdString();
    return cfg;
  }

  void EpsilonPatchExporter::export_map (EpsilonExportConfig const& cfg
                                        , std::filesystem::path const& project_path
                                        , std::string const& basename)
  {
    namespace fs = std::filesystem;

    std::string const map_lower = to_lower_copy (basename);
    std::string const rel_wdt = std::string ("world/maps/") + map_lower + "/" + map_lower + ".wdt";
    std::string const rel_lgt = std::string ("world/maps/") + map_lower + "/" + map_lower + "_lgt.wdt";

    fs::path const patch_dir = cfg.patches_path / cfg.patch_name;
    fs::path const map_out_dir = patch_dir / "world" / "maps" / map_lower;

    std::error_code ec;
    fs::create_directories (map_out_dir, ec);
    if (ec)
    {
      LogError << "Epsilon patch export: failed to create directories: " << ec.message() << std::endl;
      return;
    }

    fs::path const src_wdt = project_path / "World" / "Maps" / basename / (basename + ".wdt");
    if (!fs::is_regular_file (src_wdt, ec) || ec)
    {
      LogError << "Epsilon patch export: WDT not found at " << src_wdt.string() << std::endl;
      return;
    }

    fs::path const src_lgt = project_path / "World" / "Maps" / basename / (basename + "_lgt.wdt");
    fs::path const json_path = MapLights::manifest_path_for_map (project_path, basename);
    auto const lights_manifest = MapLights::read_json (json_path);

    if (!lights_manifest)
    {
      if (!fs::is_regular_file (src_lgt, ec) || ec)
      {
        LogError << "Epsilon patch export: _lgt.wdt not found at " << src_lgt.string() << std::endl;
        return;
      }
    }

    fs::path const patch_json_path = patch_dir / "patch.json";

    QJsonObject root;
    if (QFile::exists (QString::fromStdString (patch_json_path.string())))
    {
      QFile jf (QString::fromStdString (patch_json_path.string()));
      if (!jf.open (QIODevice::ReadOnly))
      {
        LogError << "Epsilon patch export: cannot read patch.json" << std::endl;
        return;
      }
      QJsonParseError perr{};
      QJsonDocument const doc = QJsonDocument::fromJson (jf.readAll(), &perr);
      jf.close();
      if (perr.error != QJsonParseError::NoError || !doc.isObject())
      {
        LogError << "Epsilon patch export: invalid patch.json: " << perr.errorString().toStdString() << std::endl;
        return;
      }
      root = doc.object();
    }
    else
    {
      root["name"] = QString::fromStdString (cfg.patch_name);
      root["version"] = QStringLiteral ("MU-1");
      root["url"] = QString();
      root["files"] = QJsonArray();
    }

    QJsonArray files = root["files"].toArray();
    struct FileRow
    {
      std::string file_norm;
      std::uint32_t id = 0;
    };
    std::vector<FileRow> rows;
    rows.reserve (static_cast<std::size_t>(files.size()));
    std::uint32_t max_id = (cfg.starting_fdid > 0) ? (cfg.starting_fdid - 1) : 0;

    for (auto const& v : files)
    {
      if (!v.isObject())
        continue;
      QJsonObject o = v.toObject();
      if (!o.contains ("id") || !o.contains ("file"))
        continue;
      std::string fn = o["file"].toString().toStdString();
      normalize_manifest_path_inplace (fn);
      std::uint32_t id = static_cast<std::uint32_t>(o["id"].toVariant().toULongLong());
      rows.push_back ({ fn, id });
      max_id = std::max (max_id, id);
    }

    auto find_row = [&] (std::string const& want_norm) -> int {
      for (int i = 0; i < static_cast<int>(rows.size()); ++i)
        if (paths_equal_lower (rows[static_cast<std::size_t>(i)].file_norm, want_norm))
          return i;
      return -1;
    };

    int const i_lgt = find_row (rel_lgt);
    std::uint32_t lgt_id = 0;
    if (i_lgt >= 0)
      lgt_id = rows[static_cast<std::size_t>(i_lgt)].id;
    else
    {
      lgt_id = max_id + 1;
      max_id = lgt_id;
      rows.push_back ({ rel_lgt, lgt_id });
    }

    int const i_wdt = find_row (rel_wdt);
    std::uint32_t wdt_id = 0;
    if (cfg.wdt_fdid_override != 0)
    {
      wdt_id = cfg.wdt_fdid_override;
      max_id = std::max (max_id, wdt_id);
      if (i_wdt >= 0)
        rows[static_cast<std::size_t>(i_wdt)].id = wdt_id;
      else
        rows.push_back ({ rel_wdt, wdt_id });
    }
    else if (i_wdt >= 0)
    {
      wdt_id = rows[static_cast<std::size_t>(i_wdt)].id;
    }
    else
    {
      wdt_id = max_id + 1;
      max_id = wdt_id;
      rows.push_back ({ rel_wdt, wdt_id });
    }

    // Read WDT, patch MPHD.lgtFileDataID, write to patch
    std::vector<std::uint8_t> wdt_buf;
    {
      std::ifstream in (src_wdt, std::ios::binary);
      if (!in)
      {
        LogError << "Epsilon patch export: failed to open WDT for read" << std::endl;
        return;
      }
      in.seekg (0, std::ios::end);
      auto const sz = in.tellg();
      in.seekg (0, std::ios::beg);
      if (sz <= 0 || sz > 100 * 1024 * 1024)
      {
        LogError << "Epsilon patch export: unreasonable WDT size" << std::endl;
        return;
      }
      wdt_buf.resize (static_cast<std::size_t>(sz));
      in.read (reinterpret_cast<char*>(wdt_buf.data()), sz);
      if (!in)
      {
        LogError << "Epsilon patch export: WDT read failed" << std::endl;
        return;
      }
    }

    if (!patch_wdt_lgt_file_data_id (wdt_buf, lgt_id))
    {
      LogError << "Epsilon patch export: WDT MVER/MPHD layout not recognized; not patching lgtFileDataID" << std::endl;
      return;
    }

    fs::path const out_wdt = map_out_dir / (map_lower + ".wdt");
    fs::path const out_lgt = map_out_dir / (map_lower + "_lgt.wdt");
    {
      std::ofstream out (out_wdt, std::ios::binary | std::ios::trunc);
      if (!out)
      {
        LogError << "Epsilon patch export: cannot write " << out_wdt.string() << std::endl;
        return;
      }
      out.write (reinterpret_cast<char const*>(wdt_buf.data()), static_cast<std::streamsize>(wdt_buf.size()));
    }
    if (lights_manifest)
    {
      if (!MapLights::write_lgt_wdt (*lights_manifest, out_lgt))
      {
        LogError << "Epsilon patch export: failed to write _lgt.wdt from JSON manifest." << std::endl;
        return;
      }
    }
    else
    {
      std::error_code ec2;
      fs::copy_file (src_lgt, out_lgt, fs::copy_options::overwrite_existing, ec2);
      if (ec2)
      {
        LogError << "Epsilon patch export: copy _lgt.wdt failed: " << ec2.message() << std::endl;
        return;
      }
    }

    if (lights_manifest)
      MapLightsJsonInjector::inject_ngpl_caps (*lights_manifest, cfg, basename);

    // Prune missing files from manifest (paths relative to patch root)
    {
      std::vector<FileRow> pruned;
      pruned.reserve (rows.size());
      for (auto const& r : rows)
      {
        fs::path const check = patch_dir / fs::path (r.file_norm);
        if (fs::is_regular_file (check, ec) || ec)
        {
          // If file missing, drop (unless it's one we just care about - we wrote wdt/lgt)
          if (r.file_norm == rel_wdt || r.file_norm == rel_lgt)
            pruned.push_back (r);
          else if (fs::is_regular_file (check))
            pruned.push_back (r);
        }
        else if (fs::is_regular_file (check))
          pruned.push_back (r);
      }
      // Simpler: keep row if file exists OR is wdt/lgt we ship
      pruned.clear();
      for (auto const& r : rows)
      {
        fs::path const check = patch_dir / fs::path (r.file_norm);
        if (r.file_norm == rel_wdt || r.file_norm == rel_lgt)
        {
          pruned.push_back (r);
          continue;
        }
        std::error_code ec3;
        if (fs::is_regular_file (check, ec3) && !ec3)
          pruned.push_back (r);
      }
      rows = std::move (pruned);
    }

    // Bump MU version
    {
      QString ver = root["version"].toString();
      int n = parse_mu_version (ver);
      if (n <= 0)
        n = 1;
      else
        ++n;
      root["version"] = QStringLiteral ("MU-%1").arg (n);
    }

    QJsonArray out_files;
    for (auto const& r : rows)
    {
      QJsonObject o;
      o["id"] = static_cast<qint64>(r.id);
      o["file"] = QString::fromStdString (r.file_norm);
      out_files.append (o);
    }
    root["files"] = out_files;

    {
      QFile jf (QString::fromStdString (patch_json_path.string()));
      if (!jf.open (QIODevice::WriteOnly | QIODevice::Truncate))
      {
        LogError << "Epsilon patch export: cannot write patch.json" << std::endl;
        return;
      }
      jf.write (QJsonDocument (root).toJson (QJsonDocument::Indented));
      jf.close();
    }

    Log << "Epsilon patch export: updated " << patch_json_path.string() << " (WDT fdid " << wdt_id << ", _lgt fdid " << lgt_id << ")" << std::endl;
  }
}
