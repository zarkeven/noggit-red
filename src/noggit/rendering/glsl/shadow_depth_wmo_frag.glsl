// This file is part of Noggit3, licensed under GNU General Public License (version 3).
#version 410 core

out vec4 out_color;

void main()
{
  out_color = vec4(gl_FragCoord.z, 0.0, 0.0, 1.0);
}
