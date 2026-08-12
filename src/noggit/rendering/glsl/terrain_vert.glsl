// This file is part of Noggit3, licensed under GNU General Public License (version 3).
#version 410 core

const float TILESIZE = 533.33333;
const float CHUNKSIZE = 533.33333 / 16.0;

in vec2 position;
in vec2 texcoord;

uniform mat4 model_view;
uniform mat4 projection;

struct ChunkInstanceData
{
  ivec4 ChunkTextureSamplers;
  ivec4 ChunkTextureArrayIDs;
  ivec4 ChunkHoles_DrawImpass_TexLayerCount_CantPaint;
  ivec4 ChunkTexDoAnim;
  ivec4 ChunkTexAnimSpeed;
  ivec4 AreaIDColor_Pad2_DrawSelection;
  ivec4 ChunkXZ_TileXZ;
  ivec4 ChunkTexAnimDir;

  // Mists Heightmapping

  ivec4 ChunkHeightTextureSamplers;
  ivec4 ChunkTextureUVScale;
  vec4 ChunkTextureHeightScale;
  vec4 ChunkTextureHeightOffset;

  vec4 ChunkGroundEffectColor;
  ivec4 ChunkDoodadsEnabled2_ChunksLayerEnabled2;
};

layout (std140) uniform chunk_instances
{
  ChunkInstanceData instances[256];
};

uniform sampler2D heightmap;
uniform sampler2D mccv;
uniform int base_instance;
uniform int animtime;
uniform int lod_level;

out vec3 vary_position;
out vec2 vary_texcoord;
out vec2 vary_t0_uv;
out vec2 vary_t1_uv;
out vec2 vary_t2_uv;
out vec2 vary_t3_uv;
out vec3 vary_mccv;
out vec3 vary_normal;
flat out int instanceID;
flat out vec3 triangle_normal;

// MoP+ stores an 8×8 unit hole mask. Classic 4×4 is expanded to 8×8 on upload.
// hole_lo / hole_hi are the low/high 32 bits (bytes: one bit per unit, row-major).
bool holeBitSet(uint hole_lo, uint hole_hi, uint ux, uint uy)
{
  uint bit = uy * 8u + ux;
  if (bit < 32u)
    return ((hole_lo >> bit) & 1u) != 0u;
  return ((hole_hi >> (bit - 32u)) & 1u) != 0u;
}

uint downsampleHole4x4(uint hole_lo, uint hole_hi)
{
  // OR each 2×2 of the 8×8 mask into a classic 4×4 bitfield (for LOD1).
  uint mask = 0u;
  for (uint y = 0u; y < 4u; ++y)
  {
    for (uint x = 0u; x < 4u; ++x)
    {
      bool any = holeBitSet(hole_lo, hole_hi, x * 2u, y * 2u)
              || holeBitSet(hole_lo, hole_hi, x * 2u + 1u, y * 2u)
              || holeBitSet(hole_lo, hole_hi, x * 2u, y * 2u + 1u)
              || holeBitSet(hole_lo, hole_hi, x * 2u + 1u, y * 2u + 1u);
      if (any)
        mask |= 1u << (y * 4u + x);
    }
  }
  return mask;
}

bool isHoleVertex(uint vertexId, uint hole_lo, uint hole_hi)
{
  if (hole_lo == 0u && hole_hi == 0u)
  {
    return false;
  }

  switch(lod_level)
  {
    case 0:
    {
      // Full-detail mesh: odd height rows hold the 8 unit-center verts.
      // vertex = uy * 17 + 9 + ux  (uy,ux in 0..7).
      uint rem = vertexId % 17u;
      if (rem < 9u)
        return false;
      uint ux = rem - 9u;
      uint uy = vertexId / 17u;
      if (ux >= 8u || uy >= 8u)
        return false;
      return holeBitSet(hole_lo, hole_hi, ux, uy);
    }
    case 1:
    {
      // Coarse LOD still uses the classic 4×4 vertex punches.
      uint hole = downsampleHole4x4(hole_lo, hole_hi);
      uint blockRow = vertexId / 34u;
      uint shiftedHole = hole >> (blockRow * 4u);

      if ((shiftedHole & 0x1u) != 0u)
      {
        if (vertexId == 18u || vertexId == 52u || vertexId == 86u || vertexId == 120u)
          return true;
      }
      if ((shiftedHole & 0x2u) != 0u)
      {
        if (vertexId == 20u || vertexId == 54u || vertexId == 88u || vertexId == 122u)
          return true;
      }
      if ((shiftedHole & 0x4u) != 0u)
      {
        if (vertexId == 22u || vertexId == 56u || vertexId == 90u || vertexId == 124u)
          return true;
      }
      if ((shiftedHole & 0x8u) != 0u)
      {
        if (vertexId == 24u || vertexId == 58u || vertexId == 92u || vertexId == 126u)
          return true;
      }
      break;
    }
  }

  return false;
}

float makeNaN(float nonneg)
{
  return sqrt(-nonneg-1.0);
}

vec2 animUVOffset(int do_animate, int spd, int dir)
{
  const float texanimxtab[8] = float[8]( 0, 1, 1, 1, 0, -1, -1, -1 );
  const float texanimytab[8] = float[8]( 1, 1, 0, -1, -1, -1, 0, 1 );
  float fdx = -texanimxtab[dir];
  float fdy = texanimytab[dir];
  int animspd = int(200 * 8.0);
  float f = float((int(animtime*(spd / 7.0f))) % animspd) / float(animspd);

  return vec2(f * fdx, f * fdy);
}

void main()
{
  int t_x = gl_InstanceID / 16;
  int t_z = gl_InstanceID % 16;

  instanceID = base_instance + (t_x * 16 + t_z);
  vec4 normal_pos = texelFetch(heightmap, ivec2(gl_VertexID, instanceID), 0);

  vec3 pos = vec3(instances[instanceID].ChunkXZ_TileXZ.z * TILESIZE
                      + instances[instanceID].ChunkXZ_TileXZ.x * CHUNKSIZE
                      + position.x
                    , normal_pos.a
                    , instances[instanceID].ChunkXZ_TileXZ.w * TILESIZE
                      + instances[instanceID].ChunkXZ_TileXZ.y * CHUNKSIZE
                      + position.y
                    );

  bool is_hole = isHoleVertex(uint(gl_VertexID)
                            , uint(instances[instanceID].ChunkHoles_DrawImpass_TexLayerCount_CantPaint.r)
                            , uint(instances[instanceID].AreaIDColor_Pad2_DrawSelection.b));

  float NaN = makeNaN(1);

  vec4 pos_after_holecheck = (is_hole ? vec4(NaN, NaN, NaN, 1.0) : vec4(pos, 1.0));

  gl_Position = projection * model_view * pos_after_holecheck;

  vary_normal = normal_pos.rgb;
  triangle_normal = normal_pos.rgb;
  vary_position = pos;
  vary_mccv = texelFetch(mccv, ivec2(gl_VertexID, instanceID), 0).rgb;

  vary_t0_uv = texcoord + animUVOffset(instances[instanceID].ChunkTexDoAnim.r,
    instances[instanceID].ChunkTexAnimSpeed.r, instances[instanceID].ChunkTexAnimDir.r);

  vary_t1_uv = texcoord + animUVOffset(instances[instanceID].ChunkTexDoAnim.g,
    instances[instanceID].ChunkTexAnimSpeed.g, instances[instanceID].ChunkTexAnimDir.g);

  vary_t2_uv = texcoord + animUVOffset(instances[instanceID].ChunkTexDoAnim.b,
    instances[instanceID].ChunkTexAnimSpeed.b, instances[instanceID].ChunkTexAnimDir.b);

  vary_t3_uv = texcoord + animUVOffset(instances[instanceID].ChunkTexDoAnim.a,
    instances[instanceID].ChunkTexAnimSpeed.a, instances[instanceID].ChunkTexAnimDir.a);

  vary_texcoord = texcoord;

}
