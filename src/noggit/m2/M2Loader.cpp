// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/m2/M2Loader.hpp>

#include <noggit/Log.h>
#include <noggit/application/NoggitApplication.hpp>
#include <noggit/format/ChunkReader.hpp>
#include <noggit/project/CurrentProject.hpp>

#include <ClientData.hpp>
#include <ClientFile.hpp>
#include <Listfile.hpp>

#include <cstring>
#include <stdexcept>
#include <vector>

namespace Noggit::M2
{
  namespace
  {
    using ChunkHeader = Noggit::Format::ChunkHeader;

    constexpr std::uint32_t kMd20Magic = Noggit::Format::make_fourcc('M', 'D', '2', '0');
    constexpr std::uint32_t kMd21Magic = Noggit::Format::make_fourcc('M', 'D', '2', '1');

    void read_u32_array(
      BlizzardArchive::ClientFile& file
    , ChunkHeader const& chunk
    , std::vector<std::uint32_t>& out)
    {
      if (chunk.size % sizeof(std::uint32_t) != 0)
      {
        throw std::runtime_error("M2 chunk size is not a multiple of uint32_t");
      }

      out.resize(chunk.size / sizeof(std::uint32_t));
      if (!out.empty())
      {
        file.seek(chunk.data_pos);
        file.read(out.data(), chunk.size);
      }
    }

    void parse_md21_chunk(
      LoadedData& out
    , BlizzardArchive::ClientFile& file
    , ChunkHeader const& chunk)
    {
      if (chunk.size < sizeof(ModelHeader))
      {
        throw std::runtime_error("MD21 chunk too small for ModelHeader");
      }

      file.seek(chunk.data_pos);
      std::memcpy(&out.header, file.getPointer(), sizeof(ModelHeader));

      if (std::memcmp(out.header.id, "MD20", 4) != 0)
      {
        throw std::runtime_error("MD21 chunk does not contain MD20 header");
      }

      out.data_base = reinterpret_cast<std::uint8_t const*>(file.getBuffer()) + chunk.data_pos;
      out.data_size = chunk.size;
      out.is_md21 = true;
    }

    void load_legacy_md20(LoadedData& out, BlizzardArchive::ClientFile& file)
    {
      file.seek(0);
      std::memcpy(&out.header, file.getBuffer(), sizeof(ModelHeader));

      if (std::memcmp(out.header.id, "MD20", 4) != 0)
      {
        throw std::runtime_error("File does not start with MD20 header");
      }

      out.data_base = reinterpret_cast<std::uint8_t const*>(file.getBuffer());
      out.data_size = file.getSize();
      out.is_md21 = false;
    }

    void load_chunked(LoadedData& out, BlizzardArchive::ClientFile& file, std::string const& path)
    {
      std::size_t const file_end = file.getSize();
      bool found_md21 = false;

      file.seek(0);
      try
      {
        while (file.getPos() < file_end)
        {
          ChunkHeader chunk;
          if (!Noggit::Format::read_chunk_header(file, file_end, chunk))
          {
            break;
          }

          switch (chunk.fourcc)
          {
          case kMd21Magic:
            parse_md21_chunk(out, file, chunk);
            found_md21 = true;
            Noggit::Format::skip_chunk_data(file, chunk.size, file_end);
            break;
          case Noggit::Format::make_fourcc('T', 'X', 'I', 'D'):
            read_u32_array(file, chunk, out.texture_file_data_ids);
            // read_u32_array already leaves the cursor at the end of the payload.
            break;
          case Noggit::Format::make_fourcc('S', 'F', 'I', 'D'):
            read_u32_array(file, chunk, out.skin_file_data_ids);
            break;
          case Noggit::Format::make_fourcc('A', 'F', 'I', 'D'):
            read_u32_array(file, chunk, out.anim_file_data_ids);
            break;
          case Noggit::Format::make_fourcc('P', 'F', 'I', 'D'):
          case Noggit::Format::make_fourcc('B', 'F', 'I', 'D'):
          case Noggit::Format::make_fourcc('S', 'K', 'I', 'D'):
          case Noggit::Format::make_fourcc('T', 'X', 'A', 'C'):
          case Noggit::Format::make_fourcc('E', 'X', 'P', 'T'):
          case Noggit::Format::make_fourcc('E', 'X', 'P', '2'):
          case Noggit::Format::make_fourcc('P', 'A', 'B', 'C'):
          case Noggit::Format::make_fourcc('P', 'A', 'D', 'C'):
          case Noggit::Format::make_fourcc('P', 'S', 'B', 'C'):
          case Noggit::Format::make_fourcc('P', 'E', 'D', 'C'):
          case Noggit::Format::make_fourcc('L', 'D', 'V', '1'):
          case Noggit::Format::make_fourcc('R', 'P', 'I', 'D'):
          case Noggit::Format::make_fourcc('G', 'P', 'I', 'D'):
          case Noggit::Format::make_fourcc('W', 'F', 'V', '1'):
          case Noggit::Format::make_fourcc('W', 'F', 'V', '2'):
          case Noggit::Format::make_fourcc('W', 'F', 'V', '3'):
          case Noggit::Format::make_fourcc('P', 'G', 'D', '1'):
          case Noggit::Format::make_fourcc('P', 'F', 'D', 'C'):
          case Noggit::Format::make_fourcc('E', 'D', 'G', 'F'):
          case Noggit::Format::make_fourcc('N', 'E', 'R', 'F'):
          case Noggit::Format::make_fourcc('D', 'E', 'T', 'L'):
          case Noggit::Format::make_fourcc('D', 'B', 'O', 'C'):
          case Noggit::Format::make_fourcc('A', 'F', 'R', 'A'):
          case Noggit::Format::make_fourcc('P', 'C', 'O', 'L'):
          case Noggit::Format::make_fourcc('D', 'P', 'I', 'V'):
          case Noggit::Format::make_fourcc('T', 'E', 'X', 'L'):
            Noggit::Format::skip_chunk_data(file, chunk.size, file_end);
            break;
          default:
            Noggit::Format::log_unknown_chunk_once(path, chunk.fourcc, chunk.size);
            Noggit::Format::skip_chunk_data(file, chunk.size, file_end);
            break;
          }
        }
      }
      catch (std::exception const& e)
      {
        // Keep MD21/TXID already parsed — trailing unknown/corrupt chunks must not reject the model.
        if (!found_md21)
        {
          throw;
        }
        LogError << "M2 chunk walk stopped early for '" << path << "': " << e.what() << std::endl;
      }

      if (!found_md21)
      {
        throw std::runtime_error("Chunked M2 file missing MD21 chunk");
      }
    }
  }

  bool is_valid_m2_version(
    std::uint32_t packed_version
  , Noggit::Project::ProjectVersion project_version)
  {
    switch (project_version)
    {
    case Noggit::Project::ProjectVersion::WOTLK:
      return packed_version == m2_version_wrath;
    case Noggit::Project::ProjectVersion::SL:
    case Noggit::Project::ProjectVersion::LEGION:
    case Noggit::Project::ProjectVersion::BFA:
      return packed_version >= m2_version_cataclysm && packed_version <= 280;
    case Noggit::Project::ProjectVersion::CATA:
    case Noggit::Project::ProjectVersion::PANDARIA:
    case Noggit::Project::ProjectVersion::WOD:
      return packed_version >= m2_version_cataclysm && packed_version <= m2_version_legion_bfa_sl;
    default:
      return packed_version >= m2_version_classic && packed_version <= 280;
    }
  }

  std::string resolve_file_data_id_path(std::uint32_t file_data_id)
  {
    if (file_data_id == 0)
    {
      return {};
    }

    auto* client = Noggit::Application::NoggitApplication::instance()->clientData();
    BlizzardArchive::Listfile::FileKey key(file_data_id);
    key.deduceOtherComponent(client->listfile());
    if (!key.hasFilepath())
    {
      return {};
    }
    return key.filepath();
  }

  LoadedData load(
    BlizzardArchive::ClientFile& file
  , BlizzardArchive::Listfile::FileKey const& file_key)
  {
    LoadedData out;

    if (file.isEof() || file.getSize() < 4)
    {
      throw std::runtime_error("M2 file is empty or too small");
    }

    std::uint32_t const first_magic = *reinterpret_cast<std::uint32_t const*>(file.getBuffer());
    std::string const path = file_key.hasFilepath()
      ? file_key.filepath()
      : std::to_string(file_key.fileDataID());

    if (first_magic == kMd20Magic)
    {
      load_legacy_md20(out, file);
    }
    else
    {
      load_chunked(out, file, path);
    }

    return out;
  }
}
