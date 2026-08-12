#version 410 core

layout (location = 0) in vec2 quad_corner;
layout (location = 1) in vec4 instance_center;

uniform mat4 model_view;
uniform mat4 projection;
uniform vec3 camera_pos;
uniform vec2 billboard_half_extent;

out vec2 v_uv;

void main()
{
  vec3 P = instance_center.xyz;

  vec3 V = camera_pos - P;
  float vlen = length(V);
  if (vlen > 1e-4)
  {
    V /= vlen;
  }
  else
  {
    V = vec3(0.0, 1.0, 0.0);
  }

  vec3 up0 = vec3(0.0, 1.0, 0.0);
  vec3 right = normalize(cross(up0, V));
  if (length(right) < 1e-4)
  {
    right = vec3(1.0, 0.0, 0.0);
  }
  vec3 up = normalize(cross(V, right));

  vec3 world = P + right * (quad_corner.x * billboard_half_extent.x)
             + up * (quad_corner.y * billboard_half_extent.y);

  gl_Position = projection * model_view * vec4(world, 1.0);

  v_uv = quad_corner * 0.5 + 0.5;
}
