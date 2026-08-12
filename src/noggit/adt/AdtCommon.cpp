// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/adt/AdtCommon.hpp>
#include <noggit/Misc.h>

#include <ClientFile.hpp>

namespace Noggit::Adt
{
  std::string split_adt_path(std::string const& root_adt_path, char const* suffix)
  {
    auto const dot = root_adt_path.rfind('.');
    if (dot == std::string::npos)
      return root_adt_path + suffix;
    return root_adt_path.substr(0, dot) + suffix + root_adt_path.substr(dot);
  }

  std::uint32_t read_holes_from_header(
    mcnk_flags const& flags
  , std::uint64_t holes_high_res
  , std::uint16_t holes_low_res)
  {
    if (flags.flags.high_res_holes)
    {
      // 8 bytes at MCNK+0x14: one bit per 1/8 chunk cell; collapse 2x2 blocks to the 4x4 hole mask.
      // Read as bytes (not uint64 shifts) to match client endianness — see wowdev ADT/v18 terrain holes.
      std::uint32_t mask = 0;
      auto const* holes_bytes = reinterpret_cast<std::uint8_t const*>(&holes_high_res);
      for (int row = 0; row < 8; ++row)
      {
        for (int col = 0; col < 8; ++col)
        {
          if ((holes_bytes[row] >> col) & 1)
          {
            int const lr = row / 2;
            int const lc = col / 2;
            mask |= 1u << (lr * 4 + lc);
          }
        }
      }
      return mask;
    }
    return holes_low_res;
  }

  void decode_holes_to_mask(
    mcnk_flags const& flags
  , std::uint64_t holes_high_res
  , std::uint16_t holes_low_res
  , std::uint32_t& out_holes_mask)
  {
    out_holes_mask = read_holes_from_header(flags, holes_high_res, holes_low_res);
  }

  std::uint64_t expand_holes_4x4_to_8x8(std::uint32_t holes_4x4)
  {
    std::uint64_t mask = 0;
    auto* holes_bytes = reinterpret_cast<std::uint8_t*>(&mask);
    for (int y = 0; y < 4; ++y)
    {
      for (int x = 0; x < 4; ++x)
      {
        if ((holes_4x4 & (1u << (y * 4 + x))) == 0)
          continue;
        for (int j = 0; j < 2; ++j)
        {
          for (int i = 0; i < 2; ++i)
          {
            int const row = y * 2 + j;
            int const col = x * 2 + i;
            holes_bytes[row] = static_cast<std::uint8_t>(holes_bytes[row] | (1u << col));
          }
        }
      }
    }
    return mask;
  }

  std::uint64_t render_holes_mask(
    mcnk_flags const& flags
  , std::uint64_t holes_high_res
  , std::uint32_t holes_4x4)
  {
    if (flags.flags.high_res_holes)
      return holes_high_res;
    return expand_holes_4x4_to_8x8(holes_4x4);
  }

  bool read_next_chunk(BlizzardArchive::ClientFile& file, std::size_t end_pos, ChunkIter& out)
  {
    std::size_t const pos = file.getPos();
    if (pos + 8 > end_pos)
      return false;

    file.read(&out.fourcc, 4);
    file.read(&out.size, 4);
    out.data_pos = file.getPos();

    if (out.data_pos + out.size > end_pos)
      return false;

    file.seek(out.data_pos + out.size);
    return true;
  }
}
