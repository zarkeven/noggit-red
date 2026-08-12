// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <ClientData.hpp>
#include <Listfile.hpp>

#include <cstdint>
#include <vector>

namespace noggit::adt
{
  //! Load archive data into \p out. Returns false if missing or read fails.
  [[nodiscard]] bool read_archive_file(BlizzardArchive::ClientData* client_data
      , BlizzardArchive::Listfile::FileKey const& key
      , std::vector<char>& out);

  /*!
   * Combine split ADT streams (root / tex0 / obj0) into one buffer compatible with
   * legacy single-file parsing (MHDR offsets relative to file offset 0x14).
   * Pass empty vectors for unused splits.
   */
  [[nodiscard]] std::vector<char> merge_split_adt_tile(std::vector<char> root_buffer
      , std::vector<char> const& tex0_buffer
      , std::vector<char> const& obj0_buffer);
}
