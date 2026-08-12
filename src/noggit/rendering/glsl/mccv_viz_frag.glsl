#version 410 core

in vec4 v_color;
in vec2 v_corner;

out vec4 out_color;

void main()
{
  if (dot(v_corner, v_corner) > 1.0)
    discard;

  float edge = 1.0 - smoothstep(0.85, 1.0, length(v_corner));
  out_color = vec4(v_color.rgb, v_color.a * edge);
}
