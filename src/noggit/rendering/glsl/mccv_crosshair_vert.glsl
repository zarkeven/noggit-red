#version 410 core

layout (location = 0) in vec2 ndc_pos;

void main()
{
  // Inputs are already in NDC (-1..1).
  gl_Position = vec4(ndc_pos, 0.0, 1.0);
}
