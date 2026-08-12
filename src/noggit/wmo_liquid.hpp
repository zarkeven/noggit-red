// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once
#include <opengl/scoped.hpp>

#include <cstddef>
#include <cstdint>

namespace BlizzardArchive
{
  class ClientFile;
}

struct CImVector
{
  std::uint8_t b;
  std::uint8_t g;
  std::uint8_t r;
  std::uint8_t a;
};

struct CArgb
{
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
  std::uint8_t a;
};

struct SMOLTile
{
  uint8_t liquid : 6;
  uint8_t fishable : 1;
  uint8_t shared : 1;
};

//! On-disk MOMT entry (wowlib SMOMaterial) — exactly 64 bytes.
struct WMOMaterialDisk
{
  union
  {
    uint32_t value;
    struct
    {
      uint32_t unlit :  1;
      uint32_t unfogged : 1;
      uint32_t unculled : 1;
      uint32_t ext_light: 1;
      uint32_t sidn :  1;
      uint32_t window :  1;
      uint32_t clamp_s :  1;
      uint32_t clamp_t : 1;
      uint32_t unused : 24;
    };
  } flags;
  uint32_t shader;
  uint32_t blend_mode;
  uint32_t texture_offset_1;
  CImVector sidn_color;
  CImVector frame_sidn_color;
  uint32_t texture_offset_2;
  CArgb diffuse_color;
  uint32_t ground_type;
  uint32_t texture_offset_3;
  uint32_t color_2;
  uint32_t flag_2;
  //! wowlib SMOMaterial::run_time_data[4] — last 16 bytes of the 0x40 disk entry.
  uint32_t runtime_data[4];
};
static_assert(sizeof(WMOMaterialDisk) == 0x40, "MOMT disk entry must be 64 bytes");

struct WMOMaterial : WMOMaterialDisk
{
  // Runtime-only indices into WMO::textures — never read from the MOMT chunk.
  uint32_t texture1 = 0;
  uint32_t texture2 = 0;
};

inline bool wmo_material_uses_second_texture(std::uint32_t shader)
{
  switch (shader)
  {
  case 3:  // Env
  case 5:  // EnvMetal
  case 6:  // TwoLayerDiffuse
  case 7:  // TwoLayerEnvMetal
  case 8:  // TwoLayerTerrain
  case 9:  // DiffuseEmissive
  case 11: // MaskedEnvMetal
  case 12: // EnvMetalEmissive
  case 13: // TwoLayerDiffuseOpaque
  case 15: // TwoLayerDiffuseEmissive
  case 17: // AdditiveMaskedEnvMetal
  case 18: // TwoLayerDiffuseMod2x
  case 19: // TwoLayerDiffuseMod2xNA
  case 20: // TwoLayerDiffuseAlpha
  case 21: // Lod
  case 23: // UnkDFShader (MapObjDiffuse_T1 + extra FDIDs)
    return true;
  default:
    return false;
  }
}

//! Pick diffuse / secondary FDIDs for modern UnkDFShader materials (wowdev shader ≥23).
inline void wmo_resolve_shader23_texture_keys(WMOMaterialDisk const& mat
                                            , std::uint32_t& out_tex1_key
                                            , std::uint32_t& out_tex2_key)
{
  // wowdev: additional texture file IDs live in color_2, flags_2, runTimeData, texture_3.
  // Prefer those for the visible atlas layer (sampled as tex_2 with MOTV1+ UVs), keep
  // texture_1 as the first slot when present.
  auto pick = [](std::initializer_list<std::uint32_t> keys) -> std::uint32_t
  {
    for (std::uint32_t k : keys)
    {
      if (k != 0)
        return k;
    }
    return 0;
  };

  out_tex1_key = mat.texture_offset_1 != 0 ? mat.texture_offset_1
                                          : pick({mat.color_2, mat.flag_2, mat.texture_offset_3
                                                , mat.texture_offset_2, mat.runtime_data[0]});

  out_tex2_key = pick({mat.color_2, mat.flag_2, mat.texture_offset_3, mat.texture_offset_2
                     , mat.runtime_data[0], mat.runtime_data[1], mat.texture_offset_1});
  if (out_tex2_key == out_tex1_key)
  {
    out_tex2_key = pick({mat.flag_2, mat.texture_offset_3, mat.texture_offset_2
                       , mat.runtime_data[0], mat.runtime_data[1], mat.runtime_data[2]});
  }
}

struct WMOLiquidHeader {
  int32_t X, Y, A, B;
  glm::vec3 pos;
  int16_t material_id;
};

struct SMOWVert
{
  std::uint8_t flow1;
  std::uint8_t flow2;
  std::uint8_t flow1Pct;
  std::uint8_t filler;
};
struct SMOMVert
{
  std::int16_t s;
  std::int16_t t;
};

struct WmoLiquidVertex {
  union
  {
    SMOWVert water_vertex;
    SMOMVert magma_vertex;
  };
  float height;
};

namespace OpenGL::Scoped
{
  struct use_program;
}

class wmo_liquid
{
public:
  wmo_liquid(BlizzardArchive::ClientFile* f, WMOLiquidHeader const& header, int group_liquid, bool use_dbc_type, bool is_ocean);
  wmo_liquid(wmo_liquid const& other);

  void upload(OpenGL::Scoped::use_program& water_shader);

private:
  int initGeometry(BlizzardArchive::ClientFile* f);

  glm::vec3 pos;
  bool mTransparency;
  int xtiles, ytiles;
  int _liquid_id;

  std::vector<float> depths;
  std::vector<glm::vec2> tex_coords;
  std::vector<glm::vec3> vertices;
  std::vector<std::uint16_t> indices;

  int _indices_count;

  bool _uploaded = false;

  OpenGL::Scoped::deferred_upload_buffers<4> _buffer;
  GLuint const& _indices_buffer = _buffer[0];
  GLuint const& _vertices_buffer = _buffer[1];
  GLuint const& _depth_buffer = _buffer[2];
  GLuint const& _tex_coord_buffer = _buffer[3];
  OpenGL::Scoped::deferred_upload_vertex_arrays<1> _vertex_array;
  GLuint const& _vao = _vertex_array[0];
};
