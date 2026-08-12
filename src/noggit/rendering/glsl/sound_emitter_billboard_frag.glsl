#version 410 core

uniform sampler2D icon_texture;

in vec2 v_uv;

out vec4 out_color;

void main()
{
  vec4 t = texture(icon_texture, v_uv);
  if (t.a < 0.04)
    discard;

  out_color = vec4(t.rgb, t.a);
}
