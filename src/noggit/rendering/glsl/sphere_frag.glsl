#version 330 core

uniform vec4 color;
uniform int upper_hemisphere;

in float v_local_y;

out vec4 out_color;

void main()
{
    if (upper_hemisphere != 0 && v_local_y < 0.0)
    {
        discard;
    }

    if(gl_FragCoord.x < 0.5)
    {
        discard;
    }
    out_color = color;
}
