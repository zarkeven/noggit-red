#version 330 core

in vec4 position;

uniform mat4 model_view_projection;
uniform vec3 origin;
uniform float radius;

out float v_local_y;

void main()
{
    vec4 pos = position;
    v_local_y = pos.y;
    pos.xyz *= radius;
    pos.xyz += origin;
    gl_Position = model_view_projection * pos;
}
