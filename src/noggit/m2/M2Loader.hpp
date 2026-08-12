// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/ModelHeaders.h>
#include <noggit/project/ApplicationProject.h>

#include <cstdint>
#include <string>
#include <vector>

namespace BlizzardArchive
{
  class ClientFile;
}

namespace BlizzardArchive::Listfile
{
  class FileKey;
}

namespace Noggit::M2
{
  struct LoadedData
  {
    std::uint8_t const* data_base = nullptr;
    std::size_t data_size = 0;
    ModelHeader header{};
    bool is_md21 = false;

    std::vector<std::uint32_t> texture_file_data_ids;
    std::vector<std::uint32_t> skin_file_data_ids;
    std::vector<std::uint32_t> anim_file_data_ids;
  };

  [[nodiscard]] LoadedData load(
    BlizzardArchive::ClientFile& file
  , BlizzardArchive::Listfile::FileKey const& file_key);

  [[nodiscard]] bool is_valid_m2_version(
    std::uint32_t packed_version
  , Noggit::Project::ProjectVersion project_version);

  [[nodiscard]] std::string resolve_file_data_id_path(std::uint32_t file_data_id);
}
