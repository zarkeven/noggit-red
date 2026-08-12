// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/format/ChunkReader.hpp>
#include <noggit/Log.h>

#include <ClientFile.hpp>

#include <mutex>
#include <stdexcept>
#include <unordered_set>

namespace Noggit::Format
{
  namespace
  {
    std::mutex g_unknown_chunk_log_mutex;
    std::unordered_set<std::string> g_logged_unknown_chunks;
  }

  std::string fourcc_to_string(std::uint32_t fourcc)
  {
    char chars[5];
    chars[0] = static_cast<char>((fourcc >> 0) & 0xFF);
    chars[1] = static_cast<char>((fourcc >> 8) & 0xFF);
    chars[2] = static_cast<char>((fourcc >> 16) & 0xFF);
    chars[3] = static_cast<char>((fourcc >> 24) & 0xFF);
    chars[4] = '\0';
    return std::string(chars);
  }

  bool read_chunk_header(
    BlizzardArchive::ClientFile& file
  , std::size_t end_pos
  , ChunkHeader& out)
  {
    std::size_t const pos = file.getPos();
    if (pos + 8 > end_pos)
    {
      return false;
    }

    if (file.read(&out.fourcc, 4) != 4 || file.read(&out.size, 4) != 4)
    {
      return false;
    }

    out.data_pos = file.getPos();
    if (out.data_pos + out.size > end_pos)
    {
      throw std::runtime_error(
        "Chunk '" + fourcc_to_string(out.fourcc) + "' overruns container (size="
        + std::to_string(out.size) + ")");
    }

    return true;
  }

  void skip_chunk_data(
    BlizzardArchive::ClientFile& file
  , std::uint32_t size
  , std::size_t end_pos)
  {
    std::size_t const next = file.getPos() + size;
    if (next > end_pos)
    {
      throw std::runtime_error("Chunk payload overruns container");
    }
    file.seek(next);
  }

  void log_unknown_chunk_once(
    std::string const& file_path
  , std::uint32_t fourcc
  , std::uint32_t size)
  {
    std::string const key = file_path + "|" + fourcc_to_string(fourcc);
    std::lock_guard<std::mutex> const lock(g_unknown_chunk_log_mutex);
    if (g_logged_unknown_chunks.insert(key).second)
    {
      LogDebug << "Unknown chunk '" << fourcc_to_string(fourcc) << "' (size=" << size
               << ") in '" << file_path << "'" << std::endl;
    }
  }
}
