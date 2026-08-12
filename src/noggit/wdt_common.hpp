#pragma once

#include <ClientFile.hpp>

#include <algorithm>
#include <cstdint>

namespace Noggit::Wdt
{
  inline bool read_chunk_header(BlizzardArchive::ClientFile& file, std::uint32_t& fourcc, std::uint32_t& size)
  {
    if (file.read(&fourcc, 4) != 4)
      return false;
    if (file.read(&size, 4) != 4)
      return false;
    if (size > file.getSize() - file.getPos())
      return false;
    return true;
  }

  inline void skip_chunk(BlizzardArchive::ClientFile& file, std::uint32_t size)
  {
    file.seekRelative(size);
  }

  inline bool read_mphd_flags(BlizzardArchive::ClientFile& file, std::uint32_t& out_flags)
  {
    file.seek(0);
    std::uint32_t fourcc = 0;
    std::uint32_t size = 0;

    while (!file.isEof())
    {
      if (!read_chunk_header(file, fourcc, size))
        break;

      if (fourcc == 'MPHD')
      {
        if (size < sizeof(std::uint32_t))
          return false;
        file.read(&out_flags, sizeof(std::uint32_t));
        return true;
      }

      skip_chunk(file, size);
    }

    return false;
  }

  inline bool main_chunk_has_tile(BlizzardArchive::ClientFile& file)
  {
    file.seek(0);
    std::uint32_t fourcc = 0;
    std::uint32_t size = 0;

    while (!file.isEof())
    {
      if (!read_chunk_header(file, fourcc, size))
        break;

      if (fourcc == 'MAIN')
      {
        std::uint32_t const entry_size = 8;
        std::uint32_t const entries = std::min<std::uint32_t>(4096u, size / entry_size);

        for (std::uint32_t i = 0; i < entries; ++i)
        {
          std::uint32_t flags = 0;
          if (file.read(&flags, sizeof(flags)) != sizeof(flags))
            return true;
          skip_chunk(file, 4);

          if (flags & 1)
            return true;
        }

        return true;
      }

      skip_chunk(file, size);
    }

    return true;
  }
}
