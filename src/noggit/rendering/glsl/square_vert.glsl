#version 330 core

in vec4 position;

out vec3 f_pos;

uniform mat4 model_view_projection;
uniform vec3 origin;
uniform float radius;
uniform float inclination;
uniform float orientation;
/// 1: world Y is exactly origin.y (horizontal XZ plane). Skips slope math so sea level stays at true y=0.
uniform int constant_world_y;

void main()
{
    float cos_o = cos(orientation);
    float sin_o = sin(orientation);

    if (constant_world_y != 0)
    {
        float wx = (position.x * cos_o - position.z * sin_o) * radius + origin.x;
        float wz = (position.z * cos_o + position.x * sin_o) * radius + origin.z;
        gl_Position = model_view_projection * vec4(wx, origin.y, wz, 1.0);
        f_pos = position.xyz;
        return;
    }

    vec4 pos = position;

    pos.y += pos.x * tan(inclination) * radius;

    pos.x = (position.x*cos_o - position.z * sin_o) * radius;
    pos.z = (position.z*cos_o + position.x * sin_o) * radius;

    pos.xyz += origin;
    gl_Position = model_view_projection * pos;

    f_pos = position.xyz;
}