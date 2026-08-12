#version 410 core

layout (location = 0) in vec3 position;
layout (location = 1) in vec4 instance_center_radius;
layout (location = 2) in vec4 instance_color;

uniform mat4 model_view;
uniform mat4 projection;

out vec4 v_color;

void main()
{
  vec3 world = instance_center_radius.xyz + position * instance_center_radius.w;
  gl_Position = projection * model_view * vec4(world, 1.0);
  v_color = instance_color;
}
