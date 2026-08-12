// This file is part of Noggit3, licensed under GNU General Public License (version 3).
#version 410 core

in vec3 position;

uniform mat4 transform;
uniform mat4 model_view;
uniform mat4 projection;

void main()
{
  gl_Position = projection * model_view * transform * vec4(position, 1.0);
}
