#version 410 core

// World-space triangle(s) at y = 0; transform matches terrain_vert.glsl (GPU float path).

uniform mat4 model_view;
uniform mat4 projection;

layout(location = 0) in vec3 world_position;

void main()
{
  gl_Position = projection * model_view * vec4(world_position, 1.0);
}
