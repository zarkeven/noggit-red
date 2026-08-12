// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include "LiquidTextureManager.hpp"
#include "opengl/context.inl"
#include "noggit/DBC.h"
#include "noggit/Log.h"
#include "noggit/MapHeaders.h"
#include "noggit/application/NoggitApplication.hpp"
#include "noggit/application/Utils.hpp"
#include "noggit/project/CurrentProject.hpp"
#include <noggit/TextureManager.h>

#include <blizzard-database-library/include/BlizzardDatabase.h>
#include <blizzard-database-library/include/BlizzardDatabaseTable.h>
#include <structures/FileStructures.h>

#include <ClientFile.hpp>
#include <Listfile.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace Noggit::Rendering;

namespace
{
  std::string to_lower_ascii(std::string s)
  {
    for (auto& ch : s)
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
  }

  std::string repair_liquid_texture_path(std::string path)
  {
    // WDC3 string-array offsets sometimes land 4 bytes into "xtextures\...", yielding "tures\...".
    if (path.size() >= 5)
    {
      std::string lower = to_lower_ascii(path);
      if (lower.starts_with("tures/") || lower.starts_with("tures\\"))
        path.insert(0, "xtex");
    }
    return path;
  }

  // "XTextures\\river\\lake_a.1.blp" / "lake_a.%d.blp" → strip prefix for numbered loader.
  // Static single BLPs keep the full path including ".blp".
  std::string normalize_liquid_filename(std::string path)
  {
    path = repair_liquid_texture_path(std::move(path));
    auto pct = path.find('%');
    if (pct != std::string::npos)
      return path.substr(0, pct);

    std::string const lower = to_lower_ascii(path);
    if (lower.size() >= 5 && lower.ends_with(".blp"))
    {
      // Match "...name.12.blp" (classic animated strip frame).
      std::size_t end = lower.size() - 4; // before .blp
      std::size_t i = end;
      while (i > 0 && std::isdigit(static_cast<unsigned char>(lower[i - 1])))
        --i;
      if (i < end && i > 0 && lower[i - 1] == '.')
        return path.substr(0, i); // keep trailing '.'
    }
    return path;
  }

  void register_liquid_profile(tsl::robin_map<unsigned, std::tuple<GLuint, glm::vec2, int, unsigned>>& out
    , Noggit::NoggitRenderContext context
    , unsigned liquid_type_id
    , int type
    , glm::vec2 anim
    , std::string const& filename_in)
  {
    std::string filename = normalize_liquid_filename(filename_in);

    // Numbered strip ("lake_a." + "1.blp") or a single static ".blp" path from modern LiquidType.
    bool static_blp = filename.size() >= 4
      && (filename.compare(filename.size() - 4, 4, ".blp") == 0
          || filename.compare(filename.size() - 4, 4, ".BLP") == 0);
    std::string frame0 = static_blp ? filename : (filename + "1.blp");

    // If the DB2 path is still missing, fall back to classic river strip so we don't bind empty arrays.
    auto* client = Noggit::Application::NoggitApplication::instance()->clientData();
    if (!client->exists(BlizzardArchive::Listfile::FileKey(frame0)))
    {
      LogError << "Liquid texture missing for id " << liquid_type_id << " (" << frame0
               << "); falling back to lake_a." << std::endl;
      filename = "XTextures\\river\\lake_a.";
      frame0 = filename + "1.blp";
      static_blp = false;
    }

    GLuint array = 0;
    gl.genTextures(1, &array);
    gl.bindTexture(GL_TEXTURE_2D_ARRAY, array);

    blp_texture tex(frame0, context);
    tex.finishLoading();

    int width_ = tex.width();
    int height_ = tex.height();
    const unsigned mip_level = tex.mip_level();
    const bool is_uncompressed = !tex.compression_format();

    constexpr unsigned N_FRAMES = 30;
    unsigned const max_frames = static_blp ? 1u : N_FRAMES;

    if (is_uncompressed)
    {
      for (unsigned int j = 0; j < mip_level; ++j)
      {
        gl.texImage3D(GL_TEXTURE_2D_ARRAY, j, GL_RGBA8, width_, height_, static_cast<GLsizei>(max_frames), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                      nullptr);

        width_ = std::max(width_ >> 1, 1);
        height_ = std::max(height_ >> 1, 1);
      }
    }
    else
    {
      for (unsigned int j = 0; j < mip_level; ++j)
      {
        gl.compressedTexImage3D(GL_TEXTURE_2D_ARRAY, j, tex.compression_format().value(), width_, height_, static_cast<GLsizei>(max_frames),
                                0, static_cast<GLsizei>(tex.compressed_data()[j].size() * max_frames), nullptr);

        width_ = std::max(width_ >> 1, 1);
        height_ = std::max(height_ >> 1, 1);
      }
    }

    unsigned n_frames = max_frames;
    for (int j = 0; j < static_cast<int>(max_frames); ++j)
    {
      std::string const frame_path = static_blp
        ? filename
        : (filename + std::to_string(j + 1) + ".blp");

      if (!static_blp && !client->exists(BlizzardArchive::Listfile::FileKey(frame_path)))
      {
        n_frames = static_cast<unsigned>(j);
        break;
      }

      blp_texture tex_frame(frame_path, context);
      tex_frame.finishLoading();

      if (tex_frame.height() != tex.height() || tex_frame.width() != tex.width())
        LogError << "Liquid texture resolution mismatch. Make sure all textures within a liquid type use identical format." << std::endl;
      else if (tex_frame.compression_format() != tex.compression_format())
        LogError << "Liquid texture compression mismatch. Make sure all textures within a liquid type use identical format." << std::endl;
      else if (tex_frame.mip_level() != tex.mip_level())
        LogError << "Liquid texture mip level mismatch. Make sure all textures within a liquid type use identical format." << std::endl;
      else
      {
        tex_frame.uploadToArray(j);
        continue;
      }

      tex.uploadToArray(j);
    }

    // Classic lake_a / ocean_h mips below ~64px collapse into a polka-dot grid when
    // viewed from altitude (DXT + noisy foam highlights). Keep only the top mips.
    unsigned const usable_mips = mip_level > 0 ? std::min(3u, mip_level) : 1u;
    GLint const max_level = static_cast<GLint>(usable_mips - 1);
    gl.texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, max_level);
    gl.texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                     max_level > 0 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    gl.texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.texParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_LOD_BIAS, -0.75f);

    out[liquid_type_id] = std::make_tuple(array, anim, type, n_frames);
  }

  void add_fallback_liquid_profile(tsl::robin_map<unsigned, std::tuple<GLuint, glm::vec2, int, unsigned>>& out
    , Noggit::NoggitRenderContext context)
  {
    // Matches procedural-water path: lake_a strip, used when LiquidType.dbc/db2 is missing.
    unsigned const liquid_type_id = 1;
    glm::vec2 const anim{1.f, 0.f};
    int const type = 0;
    std::string const filename = "XTextures\\river\\lake_a.";
    register_liquid_profile(out, context, liquid_type_id, type, anim, filename);
  }

  bool is_non_diffuse_liquid_texture(std::string const& lower)
  {
    // Classic: Texture_0 strip + Texture_2 bump/foam. SL specials (Ardenweald, etc.) put
    // sparkle/detail on Texture_0/1 and the real surface on Texture_*diffuse*.
    return lower.find("procedural") != std::string::npos
        || lower.find("bump") != std::string::npos
        || lower.find("specular") != std::string::npos
        || lower.find("shore") != std::string::npos
        || lower.find("foam") != std::string::npos
        || lower.find("emissive") != std::string::npos
        || lower.find("density") != std::string::npos
        || lower.find("depthtex") != std::string::npos
        || lower.find("anim_") != std::string::npos
        || lower.find("anim%d") != std::string::npos
        || lower.find("sparkle") != std::string::npos
        || lower.find("detail") != std::string::npos
        || lower.find("wake") != std::string::npos;
  }

  bool looks_like_surface_diffuse(std::string const& lower)
  {
    if (lower.find("diffuse") != std::string::npos)
      return true;
    // Classic animated water/lava/slime strips.
    if (lower.find('%') != std::string::npos
        && (lower.find("lake") != std::string::npos
            || lower.find("ocean") != std::string::npos
            || lower.find("river") != std::string::npos
            || lower.find("lava") != std::string::npos
            || lower.find("slime") != std::string::npos
            || lower.find("plague") != std::string::npos))
      return true;
    // Numbered classic strip frames from LiquidTypeXTexture (lake_a.1.blp).
    if ((lower.find("lake_a.") != std::string::npos
         || lower.find("ocean_h.") != std::string::npos
         || lower.find("fast_a.") != std::string::npos
         || lower.find("\\lava\\") != std::string::npos
         || lower.find("/lava/") != std::string::npos
         || lower.find("\\slime\\") != std::string::npos
         || lower.find("/slime/") != std::string::npos)
        && lower.find(".blp") != std::string::npos)
      return true;
    // Modern stills that are the primary surface (e.g. bioluminescentwater_28.blp).
    if (lower.find("bioluminescentwater_") != std::string::npos
        && lower.find("sparkle") == std::string::npos)
      return true;
    if (lower.find("valkyrwater") != std::string::npos
        && lower.find("sparkle") == std::string::npos
        && lower.find("detail") == std::string::npos)
      return true;
    return false;
  }

  char const* classic_strip_for_soundbank(int sound_bank)
  {
    switch (sound_bank)
    {
      case liquid_basic_types_ocean: return "XTextures\\ocean\\ocean_h.";
      case liquid_basic_types_magma: return "XTextures\\lava\\lava.";
      case liquid_basic_types_slime: return "XTextures\\slime\\slime.";
      default: return "XTextures\\river\\lake_a.";
    }
  }

  std::string pick_diffuse_from_candidates(std::vector<std::string> const& candidates
    , bool prefer_first_anim_strip)
  {
    std::string best_named_diffuse;
    std::string best_anim;
    std::string best_static;
    std::string best_surface_still; // bioluminescentwater_28 etc.

    auto* client = Noggit::Application::NoggitApplication::instance()->clientData();

    for (std::string value : candidates)
    {
      if (value.empty())
        continue;
      value = repair_liquid_texture_path(std::move(value));
      std::string const lower = to_lower_ascii(value);
      if (is_non_diffuse_liquid_texture(lower))
        continue;

      // Skip paths the client cannot open (common for stale *_arden string columns).
      std::string const probe = normalize_liquid_filename(value);
      bool const static_blp = probe.size() >= 4
        && (probe.compare(probe.size() - 4, 4, ".blp") == 0
            || probe.compare(probe.size() - 4, 4, ".BLP") == 0);
      std::string const frame0 = static_blp ? probe : (probe + "1.blp");
      if (client && !client->exists(BlizzardArchive::Listfile::FileKey(frame0)))
        continue;

      if (looks_like_surface_diffuse(lower))
      {
        if (value.find('%') != std::string::npos
            || (lower.find("lake_a.") != std::string::npos
                || lower.find("ocean_h.") != std::string::npos
                || lower.find("fast_a.") != std::string::npos))
        {
          if (prefer_first_anim_strip && best_anim.empty())
            best_anim = value;
        }
        else if (lower.find("diffuse") != std::string::npos)
        {
          if (best_named_diffuse.empty())
            best_named_diffuse = std::move(value);
        }
        else if (best_surface_still.empty())
        {
          best_surface_still = std::move(value);
        }
        continue;
      }

      if (value.find('%') != std::string::npos)
      {
        if (prefer_first_anim_strip && best_anim.empty())
          best_anim = std::move(value);
      }
      else if (best_static.empty())
      {
        best_static = std::move(value);
      }
    }

    if (!best_named_diffuse.empty())
      return best_named_diffuse;
    if (!best_surface_still.empty())
      return best_surface_still;
    if (!best_anim.empty())
      return best_anim;
    return best_static;
  }

  std::string diffuse_template_from_modern_row(BlizzardDatabaseLib::Structures::BlizzardDatabaseRow const& row
    , std::vector<std::string> const& xtex_paths)
  {
    // Prefer LiquidTypeXTexture FileDataID paths (authoritative on SL+). Stale Texture[]
    // strings often point at missing *_arden.blp names and used to fall back to lake_a.
    std::vector<std::string> candidates = xtex_paths;

    static char const* const k_diffuse_keys[] = {
      "Texture_0", "Texture_1", "Texture_2", "Texture_3",
      "Texture", "TextureFilenames", "TextureFilename"
    };

    for (char const* key : k_diffuse_keys)
    {
      auto it = row.Columns.find(key);
      if (it == row.Columns.end())
        continue;

      if (!it->second.Values.empty())
      {
        for (auto const& v : it->second.Values)
          candidates.push_back(v);
      }
      else if (!it->second.Value.empty())
      {
        candidates.push_back(it->second.Value);
      }
    }

    return pick_diffuse_from_candidates(candidates, true);
  }

  int legacy_liquid_type_rank_from_modern_row(BlizzardDatabaseLib::Structures::BlizzardDatabaseRow const& row)
  {
    // SoundBank is the modern name for classic LiquidType.Type (0=water,1=ocean,2=magma,3=slime).
    auto sbit = row.Columns.find("SoundBank");
    if (sbit != row.Columns.end() && !sbit->second.Value.empty())
    {
      try
      {
        int const bank = std::stoi(sbit->second.Value);
        if (bank >= 0 && bank <= 3)
          return bank;
      }
      catch (...)
      {
      }
    }

    std::string name;
    auto nit = row.Columns.find("Name");
    if (nit != row.Columns.end())
      name = to_lower_ascii(nit->second.Value);

    if (name.find("ocean") != std::string::npos)
      return liquid_basic_types_ocean;
    if (name.find("magma") != std::string::npos || name.find("lava") != std::string::npos)
      return liquid_basic_types_magma;
    if (name.find("slime") != std::string::npos || name.find("ooze") != std::string::npos
        || name.find("plague") != std::string::npos)
      return liquid_basic_types_slime;

    return liquid_basic_types_water;
  }

  bool try_parse_float_field(BlizzardDatabaseLib::Structures::BlizzardDatabaseRow const& row
    , char const* key
    , float& out)
  {
    auto it = row.Columns.find(key);
    if (it == row.Columns.end() || it->second.Value.empty())
      return false;
    try
    {
      out = std::stof(it->second.Value);
      return true;
    }
    catch (...)
    {
      return false;
    }
  }

  glm::vec2 anim_from_modern_row(BlizzardDatabaseLib::Structures::BlizzardDatabaseRow const& row)
  {
    // Classic LiquidType.AnimationX/Y drive liquid_frag UV scroll (lava/slime) or scale/rotation (water).
    // Modern DB2 stores them as Float[0]/Float[1] (CSV: Float_0 / Float_1). Toxic slime uses ~0.15;
    // classic magma/slime use ~0.025. Defaulting to {1,0} makes lava/slime scroll ~40× too fast.
    float ax = 0.f;
    float ay = 0.f;
    bool have_x = false;
    bool have_y = false;

    auto fit = row.Columns.find("Float");
    if (fit != row.Columns.end() && fit->second.Values.size() >= 1)
    {
      try
      {
        ax = std::stof(fit->second.Values[0]);
        have_x = true;
        if (fit->second.Values.size() >= 2)
        {
          ay = std::stof(fit->second.Values[1]);
          have_y = true;
        }
      }
      catch (...)
      {
      }
    }

    if (!have_x)
      have_x = try_parse_float_field(row, "Float_0", ax) || try_parse_float_field(row, "AnimationX", ax);
    if (!have_y)
      have_y = try_parse_float_field(row, "Float_1", ay) || try_parse_float_field(row, "AnimationY", ay);

    if (have_x)
      return {ax, have_y ? ay : 0.f};

    int const type = legacy_liquid_type_rank_from_modern_row(row);
    if (type == liquid_basic_types_magma || type == liquid_basic_types_slime)
      return {0.025f, 0.025f};

    // Water / ocean / procedural: scale 1, no rotation (matches prior modern fallback).
    return {1.f, 0.f};
  }

  unsigned liquid_type_id_from_modern_row(BlizzardDatabaseLib::Structures::BlizzardDatabaseRow const& row)
  {
    if (row.RecordId > 0)
      return static_cast<unsigned>(row.RecordId);

    auto it = row.Columns.find("ID");
    if (it != row.Columns.end())
    {
      try
      {
        return static_cast<unsigned>(std::stoul(it->second.Value));
      }
      catch (...)
      {
      }
    }

    return 0;
  }

  using XTexMap = std::unordered_map<unsigned, std::vector<std::pair<int, std::string>>>;

  XTexMap load_liquid_type_xtextures(BlizzardArchive::Listfile::Listfile const* listfile
    , BlizzardDatabaseLib::BlizzardDatabaseTable& table)
  {
    XTexMap out;
    if (!listfile)
      return out;

    auto iterator = table.Records();
    while (iterator.HasRecords())
    {
      auto const& row = iterator.Next();

      unsigned liquid_type_id = 0;
      auto lit = row.Columns.find("LiquidTypeID");
      if (lit != row.Columns.end() && !lit->second.Value.empty())
      {
        try { liquid_type_id = static_cast<unsigned>(std::stoul(lit->second.Value)); }
        catch (...) {}
      }
      if (!liquid_type_id)
        continue;

      std::uint32_t fdid = 0;
      auto fit = row.Columns.find("FileDataID");
      if (fit != row.Columns.end() && !fit->second.Value.empty())
      {
        try { fdid = static_cast<std::uint32_t>(std::stoul(fit->second.Value)); }
        catch (...) {}
      }
      if (!fdid)
        continue;

      int order = 0;
      auto oit = row.Columns.find("OrderIndex");
      if (oit != row.Columns.end() && !oit->second.Value.empty())
      {
        try { order = std::stoi(oit->second.Value); }
        catch (...) {}
      }

      std::string path = listfile->getPath(fdid);
      if (path.empty())
        continue;

      out[liquid_type_id].emplace_back(order, std::move(path));
    }

    for (auto& kv : out)
    {
      std::sort(kv.second.begin(), kv.second.end()
        , [](auto const& a, auto const& b) { return a.first < b.first; });
    }

    return out;
  }

  void upload_from_modern_liquid_type_table(tsl::robin_map<unsigned, std::tuple<GLuint, glm::vec2, int, unsigned>>& out
    , Noggit::NoggitRenderContext context
    , BlizzardDatabaseLib::BlizzardDatabaseTable& table
    , XTexMap const& xtex)
  {
    // BlizzardDatabaseTable::Record(id) is RecordById — never iterate 0..Count-1 with it.
    auto iterator = table.Records();
    while (iterator.HasRecords())
    {
      auto const& row = iterator.Next();
      unsigned const liquid_type_id = liquid_type_id_from_modern_row(row);
      if (!liquid_type_id)
        continue;

      int const type = legacy_liquid_type_rank_from_modern_row(row);
      glm::vec2 anim = anim_from_modern_row(row);

      std::vector<std::string> xtex_paths;
      if (auto it = xtex.find(liquid_type_id); it != xtex.end())
      {
        xtex_paths.reserve(it->second.size());
        for (auto const& op : it->second)
          xtex_paths.push_back(op.second);
      }

      std::string db_string_template = diffuse_template_from_modern_row(row, xtex_paths);
      std::string filename;
      if (!db_string_template.empty())
        filename = db_string_template;
      else
        filename = classic_strip_for_soundbank(type);

      if (filename.empty())
        filename = classic_strip_for_soundbank(type);

      try
      {
        register_liquid_profile(out, context, liquid_type_id, type, anim, filename);
      }
      catch (std::exception const& e)
      {
        LogError << "Failed to register modern liquid textures for id " << liquid_type_id
                 << " (" << filename << "): " << e.what() << std::endl;
      }
    }
  }
} // namespace

LiquidTextureManager::LiquidTextureManager(Noggit::NoggitRenderContext context)
  : _context(context)
{
}

void LiquidTextureManager::upload()
{
  if (_uploaded)
    return;

  if (gLiquidTypeDB.getRecordCount() > 0)
  {
    for (int i = 0; i < gLiquidTypeDB.getRecordCount(); ++i)
    {
      const DBCFile::Record record = gLiquidTypeDB.getRecord(i);
      unsigned liquid_type_id = record.getInt(LiquidTypeDB::ID);
      int type = record.getInt(LiquidTypeDB::Type);
      glm::vec2 anim = {record.getFloat(LiquidTypeDB::AnimationX), record.getFloat(LiquidTypeDB::AnimationY)};
      int shader_type = record.getInt(LiquidTypeDB::ShaderType);

      std::string filename;

      if (shader_type == 3)
      {
        filename = "XTextures\\river\\lake_a.";
        anim = glm::vec2(1.f, 0.f);
      }
      else
      {
        try
        {
          std::string db_string_template = record.getString(LiquidTypeDB::TextureFilenames);
          filename = db_string_template.substr(0, db_string_template.length() - 6);
        }
        catch (...)
        {
          filename = "XTextures\\river\\lake_a.";
        }
      }

      try
      {
        register_liquid_profile(_texture_frames_map, _context, liquid_type_id, type, anim, filename);
      }
      catch (std::exception const& e)
      {
        LogError << "Failed to register liquid textures for LiquidType id " << liquid_type_id
                 << ": " << e.what() << std::endl;
      }
    }
  }
  else if (auto* project = Noggit::Project::CurrentProject::get();
           project && project->ClientDatabase)
  {
    // Shadowlands skips classic OpenDBs(); load LiquidType.db2 for textures.
    try
    {
      auto* client = Noggit::Application::NoggitApplication::instance()->clientData();
      XTexMap xtex;
      try
      {
        auto& xtable = const_cast<BlizzardDatabaseLib::BlizzardDatabaseTable&>(
          project->ClientDatabase->LoadTable("LiquidTypeXTexture", readFileAsIMemStream));
        if (xtable.RecordCount() > 0)
        {
          xtex = load_liquid_type_xtextures(client ? client->listfile() : nullptr, xtable);
          Log << "Loaded LiquidTypeXTexture paths for " << xtex.size() << " liquid types." << std::endl;
        }
        project->ClientDatabase->UnloadTable("LiquidTypeXTexture");
      }
      catch (std::exception const& e)
      {
        LogError << "Failed to load LiquidTypeXTexture.db2: " << e.what() << std::endl;
      }

      auto& table = const_cast<BlizzardDatabaseLib::BlizzardDatabaseTable&>(
        project->ClientDatabase->LoadTable("LiquidType", readFileAsIMemStream));
      if (table.RecordCount() > 0)
      {
        Log << "Loading " << table.RecordCount() << " LiquidType rows from DB2 for liquid textures." << std::endl;
        upload_from_modern_liquid_type_table(_texture_frames_map, _context, table, xtex);
      }
      project->ClientDatabase->UnloadTable("LiquidType");
    }
    catch (std::exception const& e)
    {
      LogError << "Failed to load LiquidType.db2 for liquid textures: " << e.what() << std::endl;
    }
  }

  if (_texture_frames_map.empty())
  {
    Log << "Warning: LiquidType table has no rows - registering fallback liquid textures." << std::endl;
    add_fallback_liquid_profile(_texture_frames_map, _context);
    // Extra fallbacks so ocean/magma/slime IDs don't all share water tint incorrectly.
    try
    {
      register_liquid_profile(_texture_frames_map, _context, 2, liquid_basic_types_ocean, {1.f, 0.f}, "XTextures\\ocean\\ocean_h.");
    }
    catch (...) {}
    try
    {
      register_liquid_profile(_texture_frames_map, _context, 3, liquid_basic_types_magma, {1.f, 0.f}, "XTextures\\lava\\lava.");
    }
    catch (...) {}
    try
    {
      register_liquid_profile(_texture_frames_map, _context, 4, liquid_basic_types_slime, {1.f, 0.f}, "XTextures\\slime\\slime.");
    }
    catch (...) {}
  }

  _uploaded = true;
}

LiquidTextureManager::LiquidTextureProfile const* LiquidTextureManager::findProfile(unsigned liquid_type_id) const
{
  if (auto it = _texture_frames_map.find(liquid_type_id); it != _texture_frames_map.end())
  {
    return &it->second;
  }

  if (auto it = _texture_frames_map.find(1u); it != _texture_frames_map.end())
  {
    return &it->second;
  }

  if (!_texture_frames_map.empty())
  {
    return &_texture_frames_map.begin()->second;
  }

  return nullptr;
}

void LiquidTextureManager::unload()
{
  for (auto& pair : _texture_frames_map)
  {
    GLuint array = std::get<0>(pair.second);
    gl.deleteTextures(1, &array);
  }

  _texture_frames_map.clear();

  _uploaded = false;
}
