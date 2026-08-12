// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/MapHeaders.h>

#include <cstdint>
#include <string>

namespace BlizzardArchive
{
  class ClientFile;
}

namespace Noggit::Adt
{
  [[nodiscard]] std::string split_adt_path(std::string const& root_adt_path, char const* suffix);

  [[nodiscard]] std::uint32_t read_holes_from_header(
    mcnk_flags const& flags
  , std::uint64_t holes_high_res
  , std::uint16_t holes_low_res);

  void decode_holes_to_mask(
    mcnk_flags const& flags
  , std::uint64_t holes_high_res
  , std::uint16_t holes_low_res
  , std::uint32_t& out_holes_mask);

  // Expand classic 4×4 hole bits into an 8×8 mask (each low-res cell → its 2×2 units).
  [[nodiscard]] std::uint64_t expand_holes_4x4_to_8x8(std::uint32_t holes_4x4);

  // Mask used for terrain rendering: true 8×8 when high_res_holes is set, else expanded 4×4.
  [[nodiscard]] std::uint64_t render_holes_mask(
    mcnk_flags const& flags
  , std::uint64_t holes_high_res
  , std::uint32_t holes_4x4);


  struct ChunkIter
  {
    std::uint32_t fourcc = 0;
    std::uint32_t size = 0;
    std::size_t data_pos = 0;
  };

  [[nodiscard]] bool read_next_chunk(BlizzardArchive::ClientFile& file, std::size_t end_pos, ChunkIter& out);
}
