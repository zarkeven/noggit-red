// This file is part of Noggit3, licensed under GNU General Public License (version 3).
#version 410 core

in vec4 position;
in uint batch_mapping;

uniform mat4 transform;
uniform mat4 shadow_light_view_proj;
uniform usamplerBuffer render_batches_tex;

float makeNaN(float nonneg)
{
  return sqrt(-nonneg - 1.0);
}

void main()
{
  if (!bool(batch_mapping))
  {
    gl_Position = vec4(makeNaN(1.0));
    return;
  }

  vec4 world_pos = transform * position;
  gl_Position = shadow_light_view_proj * world_pos;
}
