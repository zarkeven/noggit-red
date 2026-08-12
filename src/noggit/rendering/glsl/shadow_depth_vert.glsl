// This file is part of Noggit3, licensed under GNU General Public License (version 3).
#version 330 core

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
uniform mat4 shadow_light_view_proj;
uniform bool anim_bones;

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
  mat4 boneTransformMat = mat4(1.0);

  if (anim_bones)
  {
    boneTransformMat = mat4(0.0);
    boneTransformMat += (float(bones_weight.x) / 255.0) * get_bone_matrix(bones_indices.x);
    boneTransformMat += (float(bones_weight.y) / 255.0) * get_bone_matrix(bones_indices.y);
    boneTransformMat += (float(bones_weight.z) / 255.0) * get_bone_matrix(bones_indices.z);
    boneTransformMat += (float(bones_weight.w) / 255.0) * get_bone_matrix(bones_indices.w);
  }

  vec4 world_vertex = transform * boneTransformMat * pos;
  gl_Position = shadow_light_view_proj * world_vertex;
}
