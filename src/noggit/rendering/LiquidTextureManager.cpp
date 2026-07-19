// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include "LiquidTextureManager.hpp"
#include "opengl/context.inl"
#include "noggit/DBC.h"
#include "noggit/Log.h"
#include "noggit/MapHeaders.h"
#include "noggit/application/NoggitApplication.hpp"
#include <noggit/TextureManager.h>

#include <BlizzardDatabaseTable.h>
#include <structures/FileStructures.h>

#include <cctype>
#include <exception>
#include <string>

using namespace Noggit::Rendering;

namespace
{
  void register_liquid_profile(tsl::robin_map<unsigned, std::tuple<GLuint, glm::vec2, int, unsigned>>& out
    , Noggit::NoggitRenderContext context
    , unsigned liquid_type_id
    , int type
    , glm::vec2 anim
    , std::string const& filename)
  {
    GLuint array = 0;
    gl.genTextures(1, &array);
    gl.bindTexture(GL_TEXTURE_2D_ARRAY, array);

    blp_texture tex(filename + "1.blp", context);
    tex.finishLoading();

    int width_ = tex.width();
    int height_ = tex.height();
    const unsigned mip_level = tex.mip_level();
    const bool is_uncompressed = !tex.compression_format();

    constexpr unsigned N_FRAMES = 30;

    if (is_uncompressed)
    {
      for (unsigned int j = 0; j < mip_level; ++j)
      {
        gl.texImage3D(GL_TEXTURE_2D_ARRAY, j, GL_RGBA8, width_, height_, N_FRAMES, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                      nullptr);

        width_ = std::max(width_ >> 1, 1);
        height_ = std::max(height_ >> 1, 1);
      }
    }
    else
    {
      for (unsigned int j = 0; j < mip_level; ++j)
      {
        gl.compressedTexImage3D(GL_TEXTURE_2D_ARRAY, j, tex.compression_format().value(), width_, height_, N_FRAMES,
                                0, static_cast<GLsizei>(tex.compressed_data()[j].size() * N_FRAMES), nullptr);

        width_ = std::max(width_ >> 1, 1);
        height_ = std::max(height_ >> 1, 1);
      }
    }

    unsigned n_frames = 30;
    for (int j = 0; j < static_cast<int>(N_FRAMES); ++j)
    {
      if (!Noggit::Application::NoggitApplication::instance()->clientData()->exists(
            filename + std::to_string((j + 1)) + ".blp"))
      {
        n_frames = static_cast<unsigned>(j);
        break;
      }

      blp_texture tex_frame(filename + std::to_string(j + 1) + ".blp", context);
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

    gl.texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, mip_level - 3);
    gl.texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    gl.texParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

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

  std::string to_lower_ascii(std::string s)
  {
    for (auto& ch : s)
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
  }

  std::string diffuse_template_from_modern_row(BlizzardDatabaseLib::Structures::BlizzardDatabaseRow const& row)
  {
    auto it = row.Columns.find("Texture");
    if (it == row.Columns.end())
      return {};

    if (!it->second.Values.empty())
      return it->second.Values.front();

    return it->second.Value;
  }

  int legacy_liquid_type_rank_from_modern_row(BlizzardDatabaseLib::Structures::BlizzardDatabaseRow const& row)
  {
    std::string name;
    auto nit = row.Columns.find("Name");
    if (nit != row.Columns.end())
      name = to_lower_ascii(nit->second.Value);

    if (name.find("ocean") != std::string::npos)
      return LIQUID_OCEAN;
    if (name.find("magma") != std::string::npos || name.find("lava") != std::string::npos)
      return LIQUID_MAGMA;
    if (name.find("slime") != std::string::npos || name.find("ooze") != std::string::npos)
      return LIQUID_SLIME;

    return LIQUID_WATER;
  }

  glm::vec2 anim_from_modern_row(BlizzardDatabaseLib::Structures::BlizzardDatabaseRow const& row)
  {
    // WDC3 reader does not always materialize float-array columns; default matches legacy procedural water.
    (void)row;
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

  void upload_from_modern_liquid_type_table(tsl::robin_map<unsigned, std::tuple<GLuint, glm::vec2, int, unsigned>>& out
    , Noggit::NoggitRenderContext context
    , BlizzardDatabaseLib::BlizzardDatabaseTable& table)
  {
    for (unsigned i = 0; i < table.RecordCount(); ++i)
    {
      auto row = table.Record(i);
      unsigned const liquid_type_id = liquid_type_id_from_modern_row(row);
      if (!liquid_type_id)
        continue;

      int const type = legacy_liquid_type_rank_from_modern_row(row);
      glm::vec2 anim = anim_from_modern_row(row);

      std::string db_string_template = diffuse_template_from_modern_row(row);
      std::string filename;
      if (db_string_template.size() >= 6u)
        filename = db_string_template.substr(0, db_string_template.size() - 6u);
      else
        filename = "XTextures\\river\\lake_a.";

      if (filename.empty())
        filename = "XTextures\\river\\lake_a.";

      register_liquid_profile(out, context, liquid_type_id, type, anim, filename);
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

  if (_texture_frames_map.empty())
  {
    Log << "Warning: LiquidType table has no rows - registering fallback liquid textures (id=1)." << std::endl;
    add_fallback_liquid_profile(_texture_frames_map, _context);
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
