#version 410 core

layout (location = 0) in vec2 quad_corner;
layout (location = 1) in vec4 instance_pos;
layout (location = 2) in vec4 instance_color;

uniform mat4 model_view;
uniform mat4 projection;
// Half-extent in NDC (after perspective divide). Constant = stable on-screen size.
uniform float point_size_ndc;
// viewport width / height — keeps disks circular
uniform float aspect;

out vec4 v_color;
out vec2 v_corner;

void main()
{
  vec4 clip = projection * model_view * vec4(instance_pos.xyz, 1.0);

  vec2 ndc_offset = quad_corner * point_size_ndc;
  ndc_offset.x /= max(aspect, 1e-4);

  // Offset in clip space so size survives the perspective divide.
  clip.xy += ndc_offset * clip.w;

  gl_Position = clip;
  v_color = instance_color;
  v_corner = quad_corner;
}
