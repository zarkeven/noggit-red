// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/VolumetricFog.hpp>
#include <noggit/Log.h>
#include <noggit/MapHeaders.h>
#include <noggit/application/NoggitApplication.hpp>
#include <noggit/project/CurrentProject.hpp>

#include <ClientData.hpp>
#include <ClientFile.hpp>
#include <Listfile.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace
{
#pragma pack(push, 1)
  struct VfogRecordRaw
  {
    float color[3];
    float intensity[3];
    float unk18;
    float position[3];
    float unk28;
    float rotation[4];
    float radius[3];
    int animation_periods[4];
    std::uint32_t flags;
    std::uint32_t model_file_data_id;
    std::uint32_t fog_level;
    std::uint32_t id;
  };

  struct VfexRecordRaw
  {
    std::uint32_t unk0;
    float unk1[16];
    std::uint32_t vfog_id;
    std::uint32_t unk3;
    std::uint32_t unk4;
    std::uint32_t unk5;
    std::uint32_t unk6;
    std::uint32_t unk7;
    std::uint32_t unk8;
  };
#pragma pack(pop)
  static_assert(sizeof(VfogRecordRaw) == 0x68, "VFOG record size");
  static_assert(sizeof(VfexRecordRaw) == 0x60, "VFEX record size");

  glm::vec3 server_to_noggit(float x, float y, float z)
  {
    return glm::vec3(-y + ZEROPOINT, z, -x + ZEROPOINT);
  }

  // Server Z-up radii (sx,sy,sz) → client Y-up (cx,cy,cz) matching server_to_noggit axes.
  void server_radii_to_noggit(float sx, float sy, float sz, float& ox, float& oy, float& oz)
  {
    ox = sy;
    oy = sz;
    oz = sx;
  }

  VolumetricFogEntry make_entry(VfogRecordRaw const& raw)
  {
    VolumetricFogEntry e;
    e.id = raw.id;
    e.color = glm::vec3(raw.color[0], raw.color[1], raw.color[2]);
    e.intensity[0] = raw.intensity[0];
    e.intensity[1] = raw.intensity[1];
    e.intensity[2] = raw.intensity[2];
    e.position = server_to_noggit(raw.position[0], raw.position[1], raw.position[2]);
    server_radii_to_noggit(raw.radius[0], raw.radius[1], raw.radius[2]
                         , e.radius[0], e.radius[1], e.radius[2]);
    e.rotation[0] = raw.rotation[0];
    e.rotation[1] = raw.rotation[1];
    e.rotation[2] = raw.rotation[2];
    e.rotation[3] = raw.rotation[3];
    e.flags = raw.flags;
    e.model_file_data_id = raw.model_file_data_id;
    e.fog_level = raw.fog_level;
    return e;
  }

  void apply_vfex(std::vector<VolumetricFogEntry>& entries, VfexRecordRaw const& raw)
  {
    for (auto& e : entries)
    {
      if (e.id != raw.vfog_id)
        continue;
      e.has_vfex = true;
      e.vfex_unk[0] = raw.unk1[0];
      e.vfex_unk[1] = raw.unk1[1];
      e.vfex_unk[2] = raw.unk1[2];
      return;
    }
  }

  std::vector<VolumetricFogEntry> parse_fogs_buffer(char const* data, std::size_t len)
  {
    std::vector<VolumetricFogEntry> entries;
    std::size_t pos = 0;

    auto read_u32 = [&](std::uint32_t& out) -> bool
    {
      if (pos + 4 > len)
        return false;
      std::memcpy(&out, data + pos, 4);
      pos += 4;
      return true;
    };

    while (pos + 8 <= len)
    {
      std::uint32_t fourcc = 0;
      std::uint32_t size = 0;
      if (!read_u32(fourcc) || !read_u32(size))
        break;
      if (pos + size > len)
        break;

      if (fourcc == 'VFOG')
      {
        std::size_t const count = size / sizeof(VfogRecordRaw);
        for (std::size_t i = 0; i < count; ++i)
        {
          VfogRecordRaw raw{};
          std::memcpy(&raw, data + pos + i * sizeof(VfogRecordRaw), sizeof(raw));
          entries.push_back(make_entry(raw));
        }
      }
      else if (fourcc == 'VFEX')
      {
        std::size_t const count = size / sizeof(VfexRecordRaw);
        for (std::size_t i = 0; i < count; ++i)
        {
          VfexRecordRaw raw{};
          std::memcpy(&raw, data + pos + i * sizeof(VfexRecordRaw), sizeof(raw));
          apply_vfex(entries, raw);
        }
      }

      pos += size;
    }

    return entries;
  }

  std::vector<VolumetricFogEntry> parse_client_file(BlizzardArchive::ClientFile& file)
  {
    if (file.isEof())
      return {};
    return parse_fogs_buffer(file.getBuffer(), file.getSize());
  }
}

float volumetric_fog_shader_intensity(float intensity0, float intensity1, float /*intensity2*/)
{
  // intensity[2] is a large radius-related scale (up to ~3300) — never use as mix weight.
  // intensity[0] is the primary 0–1 factor; intensity[1] (~0.5–5) adds mid density.
  float opacity = std::clamp(intensity0, 0.f, 1.f);
  opacity = std::clamp(opacity + std::clamp(intensity1, 0.f, 6.f) * 0.15f, 0.08f, 1.f);
  return opacity;
}

std::vector<VolumetricFogEntry> load_volumetric_fogs_from_path(std::string const& absolute_path)
{
  std::ifstream in(absolute_path, std::ios::binary);
  if (!in)
    return {};

  in.seekg(0, std::ios::end);
  auto const len = static_cast<std::size_t>(in.tellg());
  in.seekg(0, std::ios::beg);
  if (len < 8)
    return {};

  std::vector<char> buf(len);
  if (!in.read(buf.data(), static_cast<std::streamsize>(len)))
    return {};

  auto entries = parse_fogs_buffer(buf.data(), buf.size());
  if (!entries.empty())
  {
    Log << "Loaded " << entries.size() << " VFOG entries from " << absolute_path << "." << std::endl;
  }
  return entries;
}

std::vector<VolumetricFogEntry> load_volumetric_fogs_from_client(
  std::string const& map_basename
, std::uint32_t fogs_file_data_id)
{
  std::vector<VolumetricFogEntry> entries;

  auto* app = Noggit::Application::NoggitApplication::instance();
  if (!app || !app->hasClientData())
    return entries;

  // Prefer a project-disk override (e.g. dumped `_fogs.wdt` under World/Maps/<map>/).
  try
  {
    if (auto* proj = Noggit::Project::CurrentProject::get())
    {
      std::filesystem::path const override_path =
        std::filesystem::path(proj->ProjectPath)
        / "World" / "Maps" / map_basename
        / (map_basename + "_fogs.wdt");
      if (std::filesystem::exists(override_path))
      {
        entries = load_volumetric_fogs_from_path(override_path.string());
        if (!entries.empty())
          return entries;
      }
    }
  }
  catch (...)
  {
  }

  std::stringstream filename;
  filename << "World\\Maps\\" << map_basename << "\\" << map_basename << "_fogs.wdt";
  std::string const rel_win = filename.str();
  std::string const rel_disk = BlizzardArchive::ClientData::normalizeFilenameInternal(rel_win);

  BlizzardArchive::ClientData* const cd = app->clientData();
  auto* listfile = cd->listfile()
    ? const_cast<BlizzardArchive::Listfile::Listfile*>(cd->listfile())
    : nullptr;

  std::uint32_t resolved_fdid = fogs_file_data_id;
  if (resolved_fdid == 0u && listfile)
  {
    resolved_fdid = listfile->getFileDataID(rel_disk);
    if (resolved_fdid == 0u)
      resolved_fdid = listfile->getFileDataID(rel_win);
  }

  bool use_fdid = false;
  BlizzardArchive::Listfile::FileKey fdid_key;
  std::string open_key = rel_disk;

  if (cd->exists(open_key) || cd->exists(rel_win))
  {
    if (!cd->exists(open_key) && cd->exists(rel_win))
      open_key = rel_win;
  }
  else if (resolved_fdid != 0u && listfile)
  {
    fdid_key = BlizzardArchive::Listfile::FileKey(resolved_fdid, listfile);
    if (cd->exists(fdid_key))
      use_fdid = true;
    else
      return entries;
  }
  else
  {
    return entries;
  }

  BlizzardArchive::ClientFile file(use_fdid ? fdid_key : open_key, cd);
  entries = parse_client_file(file);

  if (!entries.empty())
  {
    Log << "Loaded " << entries.size() << " VFOG entries from "
        << (use_fdid ? ("FDID " + std::to_string(resolved_fdid)) : open_key)
        << "." << std::endl;
  }

  return entries;
}
