// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/MapHeaders.h>
#include <noggit/adt/AdtTileReader.hpp>

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>

namespace noggit::adt
{
namespace
{
  void write_u32_le(std::vector<char>& b, std::size_t pos, std::uint32_t v)
  {
    std::memcpy(b.data() + pos, &v, 4);
  }

  [[nodiscard]] std::uint32_t read_u32_le(std::vector<char> const& b, std::size_t pos)
  {
    std::uint32_t v = 0;
    std::memcpy(&v, b.data() + pos, 4);
    return v;
  }

  [[nodiscard]] bool chunk_iterate(std::vector<char> const& buf
      , std::size_t start
      , auto&& fn)
  {
    std::size_t pos = start;
    while (pos + 8 <= buf.size())
    {
      std::uint32_t const cc = read_u32_le(buf, pos);
      std::uint32_t const sz = read_u32_le(buf, pos + 4);
      std::size_t const next = pos + 8u + static_cast<std::size_t>(sz);
      if (next > buf.size())
      {
        return false;
      }
      if (!fn(cc, pos, sz))
      {
        return true;
      }
      pos = next;
    }
    return true;
  }

  [[nodiscard]] std::optional<std::vector<char>> pull_first_chunk(std::vector<char> const& buf
      , std::uint32_t fourcc)
  {
    std::optional<std::vector<char>> found;
    chunk_iterate(buf, 0, [&](std::uint32_t cc, std::size_t off, std::uint32_t sz) -> bool {
      if (cc != fourcc)
      {
        return true;
      }
      std::size_t const total = 8u + static_cast<std::size_t>(sz);
      found.emplace(buf.begin() + static_cast<std::ptrdiff_t>(off)
                    , buf.begin() + static_cast<std::ptrdiff_t>(off + total));
      return false;
    });
    return found;
  }

  [[nodiscard]] std::vector<std::vector<char>> pull_all_chunks(std::vector<char> const& buf
      , std::uint32_t fourcc)
  {
    std::vector<std::vector<char>> out;
    chunk_iterate(buf, 0, [&](std::uint32_t cc, std::size_t off, std::uint32_t sz) -> bool {
      if (cc != fourcc)
      {
        return true;
      }
      std::size_t const total = 8u + static_cast<std::size_t>(sz);
      out.emplace_back(buf.begin() + static_cast<std::ptrdiff_t>(off)
                       , buf.begin() + static_cast<std::ptrdiff_t>(off + total));
      return true;
    });
    return out;
  }

  [[nodiscard]] bool parse_mhdr_fields(std::vector<char> const& buf, MHDR* hdr_out)
  {
    bool ok = false;
    chunk_iterate(buf, 0, [&](std::uint32_t cc, std::size_t off, std::uint32_t sz) -> bool {
      if (cc != 'MHDR')
      {
        return true;
      }
      if (sz < sizeof(MHDR))
      {
        ok = false;
        return false;
      }
      std::memcpy(hdr_out, buf.data() + off + 8, sizeof(MHDR));
      ok = true;
      return false;
    });
    return ok;
  }

  [[nodiscard]] std::uint32_t mver_version(std::vector<char> const& buf)
  {
    std::uint32_t v = 18;
    chunk_iterate(buf, 0, [&](std::uint32_t cc, std::size_t off, std::uint32_t sz) -> bool {
      if (cc != 'MVER')
      {
        return true;
      }
      if (sz >= 4)
      {
        v = read_u32_le(buf, off + 8);
      }
      return false;
    });
    return v;
  }

  // tex0/obj0 MCNKs are headerless (wowlib file_has_header); after concat, stamp
  // derived SMChunk fields so MapChunk/TextureSet can find MCLY/MCAL via offsets.
  void stamp_merged_mcnk_header(std::vector<char>& mcnk)
  {
    constexpr std::size_t k_hdr = sizeof(MapChunkHeader);
    if (mcnk.size() < 8 + k_hdr)
    {
      return;
    }

    std::uint32_t ofs_layer = 0;
    std::uint32_t n_layers = 0;
    std::uint32_t ofs_alpha = 0;
    std::uint32_t size_alpha = 0;
    std::uint32_t ofs_shadow = 0;
    std::uint32_t size_shadow = 0;
    std::uint32_t ofs_refs = 0;
    std::uint32_t n_doodad_refs = 0;
    std::uint32_t n_map_obj_refs = 0;

    std::size_t pos = 8 + k_hdr;
    std::size_t const end = mcnk.size();
    while (pos + 8 <= end)
    {
      std::uint32_t const cc = read_u32_le(mcnk, pos);
      std::uint32_t const sz = read_u32_le(mcnk, pos + 4);
      if (sz > end - (pos + 8))
      {
        break;
      }

      if (cc == 'MCLY')
      {
        ofs_layer = static_cast<std::uint32_t>(pos);
        n_layers = sz / static_cast<std::uint32_t>(sizeof(ENTRY_MCLY));
        if (n_layers > 4)
        {
          n_layers = 4;
        }
      }
      else if (cc == 'MCAL')
      {
        ofs_alpha = static_cast<std::uint32_t>(pos);
        size_alpha = 8u + sz; // wowdev: sizeAlpha includes chunk header
      }
      else if (cc == 'MCSH')
      {
        ofs_shadow = static_cast<std::uint32_t>(pos);
        size_shadow = 8u + sz;
      }
      else if (cc == 'MCRF')
      {
        ofs_refs = static_cast<std::uint32_t>(pos);
        // Prefer header counts when present; otherwise leave unchanged below.
      }
      else if (cc == 'MCRD')
      {
        n_doodad_refs = sz / 4u;
      }
      else if (cc == 'MCRW')
      {
        n_map_obj_refs = sz / 4u;
      }

      pos += 8u + static_cast<std::size_t>(sz);
    }

    MapChunkHeader hdr{};
    std::memcpy(&hdr, mcnk.data() + 8, k_hdr);
    if (ofs_layer)
    {
      hdr.ofsLayer = ofs_layer;
      hdr.nLayers = n_layers;
    }
    if (ofs_alpha)
    {
      hdr.ofsAlpha = ofs_alpha;
      hdr.sizeAlpha = size_alpha;
    }
    if (ofs_shadow)
    {
      hdr.ofsShadow = ofs_shadow;
      hdr.sizeShadow = size_shadow;
    }
    if (ofs_refs)
    {
      hdr.ofsRefs = ofs_refs;
    }
    if (n_doodad_refs)
    {
      hdr.nDoodadRefs = n_doodad_refs;
    }
    if (n_map_obj_refs)
    {
      hdr.nMapObjRefs = n_map_obj_refs;
    }
    std::memcpy(mcnk.data() + 8, &hdr, k_hdr);
  }

  [[nodiscard]] std::vector<char> merge_three_mcnk(std::vector<char> const& root_full
      , std::vector<char> const* tex_full
      , std::vector<char> const* obj_full)
  {
    if (root_full.size() < 8)
    {
      return {};
    }
    std::uint32_t const root_sz = read_u32_le(root_full, 4);
    if (8u + root_sz > root_full.size())
    {
      return {};
    }
    std::vector<char> inner;
    inner.insert(inner.end()
                 , root_full.begin() + 8
                 , root_full.begin() + 8 + static_cast<std::ptrdiff_t>(root_sz));

    auto append_extra = [&](std::vector<char> const* extra) {
      if (!extra || extra->size() < 8)
      {
        return;
      }
      std::uint32_t const esz = read_u32_le(*extra, 4);
      if (8u + esz > extra->size())
      {
        return;
      }
      // tex0/obj0 payloads start at subchunks (no SMChunk header).
      inner.insert(inner.end()
                   , extra->begin() + 8
                   , extra->begin() + 8 + static_cast<std::ptrdiff_t>(esz));
    };

    append_extra(tex_full);
    append_extra(obj_full);

    std::vector<char> chunk;
    chunk.resize(8 + inner.size());
    std::memcpy(chunk.data(), root_full.data(), 4);
    std::uint32_t const inner_sz_u32 = static_cast<std::uint32_t>(inner.size());
    std::memcpy(chunk.data() + 4, &inner_sz_u32, 4);
    std::memcpy(chunk.data() + 8, inner.data(), inner.size());
    stamp_merged_mcnk_header(chunk);
    return chunk;
  }

  void append_chunk(std::vector<char>& out, std::uint32_t fourcc, std::vector<char> const& payload)
  {
    std::size_t const at = out.size();
    out.resize(at + 8 + payload.size());
    write_u32_le(out, at, fourcc);
    write_u32_le(out, at + 4, static_cast<std::uint32_t>(payload.size()));
    if (!payload.empty())
    {
      std::memcpy(out.data() + at + 8, payload.data(), payload.size());
    }
  }

  [[nodiscard]] std::optional<std::size_t> chunk_quadruple_position(std::vector<char> const& file
      , std::uint32_t fourcc)
  {
    std::optional<std::size_t> pos;
    chunk_iterate(file, 0, [&](std::uint32_t cc, std::size_t off, std::uint32_t) -> bool {
      if (cc == fourcc)
      {
        pos = off;
        return false;
      }
      return true;
    });
    return pos;
  }

  void patch_mhdr(std::vector<char>& file, MHDR const& hdr)
  {
    chunk_iterate(file, 0, [&](std::uint32_t cc, std::size_t off, std::uint32_t sz) -> bool {
      if (cc != 'MHDR')
      {
        return true;
      }
      if (sz < sizeof(MHDR))
      {
        return false;
      }
      std::memcpy(file.data() + off + 8, &hdr, sizeof(MHDR));
      return false;
    });
  }
} // namespace

bool read_archive_file(BlizzardArchive::ClientData* client_data
    , BlizzardArchive::Listfile::FileKey const& key
    , std::vector<char>& out)
{
  out.clear();
  if (!client_data)
  {
    return false;
  }
  if (!client_data->exists(key))
  {
    return false;
  }
  return client_data->readFile(key, out);
}

std::vector<char> merge_split_adt_tile(std::vector<char> root_buffer
    , std::vector<char> const& tex0_buffer
    , std::vector<char> const& obj0_buffer)
{
  if (tex0_buffer.empty() && obj0_buffer.empty())
  {
    return root_buffer;
  }

  MHDR hdr{};
  if (!parse_mhdr_fields(root_buffer, &hdr))
  {
    return root_buffer;
  }

  std::uint32_t vr = mver_version(root_buffer);
  if (!tex0_buffer.empty())
  {
    vr = std::max(vr, mver_version(tex0_buffer));
  }
  if (!obj0_buffer.empty())
  {
    vr = std::max(vr, mver_version(obj0_buffer));
  }

  auto take_chunk = [&](std::uint32_t cc, std::vector<char> const& primary
                        , std::vector<char> const& secondary) -> std::vector<char> {
    if (auto p = pull_first_chunk(primary, cc))
    {
      return std::move(*p);
    }
    if (!secondary.empty())
    {
      if (auto p = pull_first_chunk(secondary, cc))
      {
        return std::move(*p);
      }
    }
    return {};
  };

  std::vector<char> chunk_mtex = take_chunk('MTEX', tex0_buffer, root_buffer);
  std::vector<char> chunk_mdid = take_chunk('MDID', tex0_buffer, root_buffer);
  std::vector<char> chunk_mhid = take_chunk('MHID', tex0_buffer, root_buffer);
  std::vector<char> chunk_mtxf = take_chunk('MTXF', tex0_buffer, root_buffer);
  std::vector<char> chunk_mtxp = take_chunk('MTXP', tex0_buffer, root_buffer);
  std::vector<char> chunk_mmdx = take_chunk('MMDX', obj0_buffer, root_buffer);
  std::vector<char> chunk_mmid = take_chunk('MMID', obj0_buffer, root_buffer);
  std::vector<char> chunk_mwmo = take_chunk('MWMO', obj0_buffer, root_buffer);
  std::vector<char> chunk_mwid = take_chunk('MWID', obj0_buffer, root_buffer);
  std::vector<char> chunk_mddf = take_chunk('MDDF', obj0_buffer, root_buffer);
  std::vector<char> chunk_modf = take_chunk('MODF', obj0_buffer, root_buffer);
  std::vector<char> chunk_mh2o = take_chunk('MH2O', root_buffer, {});
  std::vector<char> chunk_mfbo = take_chunk('MFBO', root_buffer, {});

  std::vector<std::vector<char>> mcnk_root = pull_all_chunks(root_buffer, 'MCNK');
  std::vector<std::vector<char>> mcnk_tex = tex0_buffer.empty()
                                               ? std::vector<std::vector<char>>{}
                                               : pull_all_chunks(tex0_buffer, 'MCNK');
  std::vector<std::vector<char>> mcnk_obj = obj0_buffer.empty()
                                               ? std::vector<std::vector<char>>{}
                                               : pull_all_chunks(obj0_buffer, 'MCNK');

  if (mcnk_root.size() != 256)
  {
    return root_buffer;
  }

  std::vector<char> out;

  {
    std::vector<char> payload(4);
    std::memcpy(payload.data(), &vr, 4);
    append_chunk(out, 'MVER', payload);
  }

  MHDR new_hdr = hdr;
  new_hdr.mcin = 0;
  new_hdr.mtex = 0;
  new_hdr.mmdx = 0;
  new_hdr.mmid = 0;
  new_hdr.mwmo = 0;
  new_hdr.mwid = 0;
  new_hdr.mddf = 0;
  new_hdr.modf = 0;
  new_hdr.mh2o = 0;
  new_hdr.mfbo = 0;
  new_hdr.mtxf = 0;

  {
    std::vector<char> body(sizeof(MHDR));
    std::memcpy(body.data(), &new_hdr, sizeof(MHDR));
    append_chunk(out, 'MHDR', body);
  }

  // Placeholder MCIN (256 × ENTRY_MCIN). Offsets filled after MCNKs are appended.
  std::size_t const mcin_chunk_off = out.size();
  {
    std::vector<char> mcin_body(256 * sizeof(ENTRY_MCIN), 0);
    append_chunk(out, 'MCIN', mcin_body);
  }

  constexpr std::uintptr_t k_smap_anchor = 0x14;
  auto rel = [&](std::vector<char> const& file_after_build, std::uint32_t cc) -> std::uint32_t {
    auto const pos = chunk_quadruple_position(file_after_build, cc);
    if (!pos)
    {
      return 0;
    }
    std::uint32_t const u = static_cast<std::uint32_t>(*pos);
    return u >= k_smap_anchor ? static_cast<std::uint32_t>(u - k_smap_anchor) : 0;
  };

  auto append_if = [&](std::vector<char>& dest, std::vector<char> const& chunk_whole) {
    if (!chunk_whole.empty())
    {
      dest.insert(dest.end(), chunk_whole.begin(), chunk_whole.end());
    }
  };

  append_if(out, chunk_mtex);
  append_if(out, chunk_mdid);
  append_if(out, chunk_mhid);
  append_if(out, chunk_mtxf);
  append_if(out, chunk_mtxp);
  append_if(out, chunk_mmdx);
  append_if(out, chunk_mmid);
  append_if(out, chunk_mwmo);
  append_if(out, chunk_mwid);
  append_if(out, chunk_mddf);
  append_if(out, chunk_modf);
  append_if(out, chunk_mh2o);
  append_if(out, chunk_mfbo);

  for (std::size_t i = 0; i < 256; ++i)
  {
    std::vector<char> const* tex_ptr = (i < mcnk_tex.size() && !mcnk_tex[i].empty())
                                         ? &mcnk_tex[i]
                                         : nullptr;
    std::vector<char> const* obj_ptr = (i < mcnk_obj.size() && !mcnk_obj[i].empty())
                                         ? &mcnk_obj[i]
                                         : nullptr;
    std::vector<char> merged = merge_three_mcnk(mcnk_root[i], tex_ptr, obj_ptr);
    if (!merged.empty())
    {
      ENTRY_MCIN entry{};
      entry.offset = static_cast<std::uint32_t>(out.size());
      entry.size = static_cast<std::uint32_t>(merged.size());
      std::memcpy(out.data() + mcin_chunk_off + 8 + i * sizeof(ENTRY_MCIN), &entry, sizeof(ENTRY_MCIN));
      out.insert(out.end(), merged.begin(), merged.end());
    }
  }

  new_hdr.mcin = rel(out, 'MCIN');
  new_hdr.mtex = rel(out, 'MTEX');
  new_hdr.mtxf = rel(out, 'MTXF');
  new_hdr.mmdx = rel(out, 'MMDX');
  new_hdr.mmid = rel(out, 'MMID');
  new_hdr.mwmo = rel(out, 'MWMO');
  new_hdr.mwid = rel(out, 'MWID');
  new_hdr.mddf = rel(out, 'MDDF');
  new_hdr.modf = rel(out, 'MODF');
  new_hdr.mh2o = rel(out, 'MH2O');
  new_hdr.mfbo = rel(out, 'MFBO');

  patch_mhdr(out, new_hdr);

  return out;
}

} // namespace noggit::adt
