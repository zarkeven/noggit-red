#version 410 core

uniform sampler2D digit_atlas;

in vec2 v_atlas_uv;
in vec4 v_color;

out vec4 out_color;

void main()
{
  vec4 t = texture(digit_atlas, v_atlas_uv);
  if (t.a < 0.04)
    discard;

  out_color = vec4(v_color.rgb, v_color.a * t.a);
}
