// This file is part of Noggit3, licensed under GNU General Public License (version 3).
#version 330 core

// Match MapHeaders.h / terrain_vert.glsl: ADT tile and MCNK outer size in world yards.
const float kWoWTilesize = 533.33333;
const float kWoWChunksize = kWoWTilesize / 16.0;

in vec4 pos;
in vec3 normal;
in vec2 texcoord1;
in vec2 texcoord2;
in uvec4 bones_weight;
in uvec4 bones_indices;

#ifdef instanced
  in mat4 transform;
#else
  uniform mat4 transform;
#endif

uniform samplerBuffer bone_matrices;

out vec2 uv1;
out vec2 uv2;
out float camera_dist;
out vec3 norm;
out vec3 world_pos;

uniform mat4 model_view;
uniform mat4 projection;


uniform int tex_unit_lookup_1;
uniform int tex_unit_lookup_2;

uniform mat4 tex_matrix_1;
uniform mat4 tex_matrix_2;

uniform bool anim_bones;

// Bit 0: apply chunk UV to texcoord path for texture unit 1; bit 1: for unit 2 (see ModelRender / terrain bind).
uniform int terrain_uv_mask;
uniform vec2 terrain_chunk_corner_xz;

// code from https://wowdev.wiki/M2/.skin#Environment_mapping
vec2 sphere_map(vec3 vert, vec3 norm)
{
  vec3 normPos = -(normalize(vert));
  vec3 temp = (normPos - (norm * (2.0 * dot(normPos, norm))));
  temp = vec3(temp.x, temp.y, temp.z + 1.0);
 
  return ((normalize(temp).xy * 0.5) + vec2(0.5));
}

vec2 chunk_terrain_uv(vec3 world_pos)
{
  return clamp((world_pos.xz - terrain_chunk_corner_xz) / vec2(kWoWChunksize), vec2(0.0), vec2(1.0));
}

vec2 get_texture_uv(int tex_unit_lookup, vec3 vert, vec3 norm, vec3 world_pos, mat4 inst_transform)
{
  if(tex_unit_lookup == 0)
  {
    return sphere_map(vert, norm);
  }
  else if(tex_unit_lookup == 1)
  {
    if ((terrain_uv_mask & 1) != 0)
    {
      return chunk_terrain_uv(world_pos);
    }
    return (transpose(tex_matrix_1) * vec4(texcoord1, 0.0, 1.0)).xy;
  }
  else if(tex_unit_lookup == 2)
  {
    if ((terrain_uv_mask & 2) != 0)
    {
      return chunk_terrain_uv(world_pos);
    }
    return (transpose(tex_matrix_2) * vec4(texcoord2, 0.0, 1.0)).xy;
  }
  else if(tex_unit_lookup == 4)
  {
    // Match prepareDraw: bit 0 = layer 0 uses chunk UV, bit 1 = layer 1. Ground lookup must honor either bit
    // (e.g. env + projected terrain on T2 with mask 0b10 only).
    if ((terrain_uv_mask & 3) != 0)
    {
      return chunk_terrain_uv(world_pos);
    }
    // Chunk-period tiling; subtract instance translation so UVs do not slide when the whole doodad is moved.
    vec2 origin_xz = inst_transform[3].xz;
    return fract((world_pos.xz - origin_xz) / kWoWChunksize);
  }
  else
  {
    return vec2(0.0);
  }
}

mat4 get_bone_matrix(uint bone_index)
{
  mat4 matrix;
  int pixel_start = int(bone_index) * 4;
  matrix[0] = texelFetch(bone_matrices, pixel_start).rgba;
  matrix[1] = texelFetch(bone_matrices, pixel_start + 1).rgba;
  matrix[2] = texelFetch(bone_matrices, pixel_start + 2).rgba;
  matrix[3] = texelFetch(bone_matrices, pixel_start + 3).rgba;

  return matrix;
}

void main()
{
  mat4 boneTransformMat = mat4(0);

  if (anim_bones)
  {
    boneTransformMat += (float(bones_weight.x) / 255.0) * get_bone_matrix(bones_indices.x);
    boneTransformMat += (float(bones_weight.y) / 255.0) * get_bone_matrix(bones_indices.y);
    boneTransformMat += (float(bones_weight.z) / 255.0) * get_bone_matrix(bones_indices.z);
    boneTransformMat += (float(bones_weight.w) / 255.0) * get_bone_matrix(bones_indices.w);
  }
  else
  {
    boneTransformMat = mat4(1);
  }

  mat4 worldMatrix = transform * boneTransformMat;
  mat4 cameraMatrix = model_view * worldMatrix;
  mat3 normMatrix = mat3(worldMatrix);

  vec4 world_vertex = worldMatrix * pos;
  vec4 vertex = model_view * world_vertex;

  // important to normalize because of the scaling !!
  norm = normalize(normMatrix * normal);
  world_pos = world_vertex.xyz;

  uv1 = get_texture_uv(tex_unit_lookup_1, vertex.xyz, norm, world_pos, transform);
  uv2 = get_texture_uv(tex_unit_lookup_2, vertex.xyz, norm, world_pos, transform);

  camera_dist = -vertex.z;
  gl_Position = projection * vertex;
}
