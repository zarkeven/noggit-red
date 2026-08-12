// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <cstdint>
#include <string>

namespace BlizzardArchive
{
  class ClientFile;
}

namespace Noggit::Format
{
  struct ChunkHeader
  {
    std::uint32_t fourcc = 0;
    std::uint32_t size = 0;
    std::size_t data_pos = 0;
  };

  [[nodiscard]] std::string fourcc_to_string(std::uint32_t fourcc);

  [[nodiscard]] bool read_chunk_header(
    BlizzardArchive::ClientFile& file
  , std::size_t end_pos
  , ChunkHeader& out);

  void skip_chunk_data(
    BlizzardArchive::ClientFile& file
  , std::uint32_t size
  , std::size_t end_pos);

  void log_unknown_chunk_once(
    std::string const& file_path
  , std::uint32_t fourcc
  , std::uint32_t size);

  [[nodiscard]] inline constexpr std::uint32_t make_fourcc(char a, char b, char c, char d)
  {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a))
         | (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8)
         | (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16)
         | (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24);
  }
}
