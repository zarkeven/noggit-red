// This file is part of Noggit3, licensed under GNU General Public License (version 3).
#version 410 core

// flags
#define eWMOBatch_ExteriorLit 0x1u
#define eWMOBatch_HasMOCV 0x2u
#define eWMOBatch_Unlit 0x4u
#define eWMOBatch_Unfogged 0x8u

layout (std140) uniform lighting
{
  vec4 DiffuseColor_FogStart;
  vec4 AmbientColor_FogEnd;
  vec4 FogColor_FogOn;
  vec4 LightDir_FogRate;
  vec4 OceanColorLight;
  vec4 OceanColorDark;
  vec4 RiverColorLight;
  vec4 RiverColorDark;
};

layout (std140) uniform point_lights
{
  ivec4 meta; // x: count, y: enabled
  vec4 position_radius[256];
  vec4 color_intensity[256];
  vec4 attenuation[256];
  vec4 spot_dir_cos_inner[256];
  vec4 spot_cos_outer_kind[256];
};

uniform vec3 camera;
uniform sampler2DArray texture_samplers[15];
uniform vec3 ambient_color;
uniform sampler2D terrain_blend_color;
uniform vec2 terrain_blend_origin_xz;
uniform float terrain_blend_inv_size;
uniform int wmo_terrain_blend_enabled;

in vec3 f_position;
in vec3 f_normal;
in vec2 f_texcoord;
in vec2 f_texcoord_2;
in vec4 f_vertex_color;

flat in uint flags;
flat in uint wmo_batch_shader;
flat in uint tex_array0;
flat in uint tex_array1;
flat in uint tex0;
flat in uint tex1;
flat in uint alpha_test_mode;

out vec4 out_color;

vec4 get_tex_color(vec2 tex_coord, uint tex_sampler, int array_index)
{
  if (tex_sampler == 0)
  {
    return texture(texture_samplers[0], vec3(tex_coord, array_index)).rgba;
  }
  else if (tex_sampler == 1)
  {
    return texture(texture_samplers[1], vec3(tex_coord, array_index)).rgba;
  }
  else if (tex_sampler == 2)
  {
    return texture(texture_samplers[2], vec3(tex_coord, array_index)).rgba;
  }
  else if (tex_sampler == 3)
  {
    return texture(texture_samplers[3], vec3(tex_coord, array_index)).rgba;
  }
  else if (tex_sampler == 4)
  {
    return texture(texture_samplers[4], vec3(tex_coord, array_index)).rgba;
  }
  else if (tex_sampler == 5)
  {
    return texture(texture_samplers[5], vec3(tex_coord, array_index)).rgba;
  }
  else if (tex_sampler == 6)
  {
    return texture(texture_samplers[6], vec3(tex_coord, array_index)).rgba;
  }
  else if (tex_sampler == 7)
  {
    return texture(texture_samplers[7], vec3(tex_coord, array_index)).rgba;
  }
  else if (tex_sampler == 8)
  {
    return texture(texture_samplers[8], vec3(tex_coord, array_index)).rgba;
  }
  else if (tex_sampler == 9)
  {
    return texture(texture_samplers[9], vec3(tex_coord, array_index)).rgba;
  }
  else if (tex_sampler == 10)
  {
    return texture(texture_samplers[10], vec3(tex_coord, array_index)).rgba;
  }
  else if (tex_sampler == 11)
  {
    return texture(texture_samplers[11], vec3(tex_coord, array_index)).rgba;
  }
  else if (tex_sampler == 12)
  {
    return texture(texture_samplers[12], vec3(tex_coord, array_index)).rgba;
  }
  else if (tex_sampler == 13)
  {
    return texture(texture_samplers[13], vec3(tex_coord, array_index)).rgba;
  }
  else if (tex_sampler == 14)
  {
    return texture(texture_samplers[14], vec3(tex_coord, array_index)).rgba;
  }

  return vec4(0);
}

vec3 apply_lighting(vec3 material)
{
  vec3 ambient_term;
  vec3 diffuse_term;
  vec3 vertex_color = bool(flags & eWMOBatch_HasMOCV) ? f_vertex_color.rgb : vec3(0.);

  if(bool(flags & eWMOBatch_Unlit))
  {
    ambient_term = vec3(0.0);
    diffuse_term = vec3(0.0);
  }
  else if(bool(flags & eWMOBatch_ExteriorLit))
  {
    ambient_term = AmbientColor_FogEnd.xyz;
    diffuse_term = DiffuseColor_FogStart.xyz;
  }
  else
  {
    ambient_term = ambient_color;
    diffuse_term = vec3(0.0);
  }

  // apply world lighting
  vec3 currColor;
  vec3 lDiffuse = vec3(0.0, 0.0, 0.0);
  vec3 accumlatedLight = vec3(1.0, 1.0, 1.0);

  if(!bool(flags & eWMOBatch_Unlit))
  {
    float nDotL = clamp(dot(normalize(f_normal), -normalize(vec3(-LightDir_FogRate.x, LightDir_FogRate.z, -LightDir_FogRate.y))), 0.0, 1.0);

    vec3 ambientColor = ambient_term + vertex_color;

    vec3 skyColor = (ambientColor * 1.10000002);
    vec3 groundColor = (ambientColor * 0.699999988);

    currColor = mix(groundColor, skyColor, 0.5 + (0.5 * nDotL));
    lDiffuse = diffuse_term * nDotL;

    if (meta.y != 0)
    {
      vec3 N = normalize(f_normal);
      for (int i = 0; i < meta.x; ++i)
      {
        vec3 L = position_radius[i].xyz - f_position;
        float dist = length(L);
        float radius = position_radius[i].w;
        float start = max(0.0, attenuation[i].x);
        float end = attenuation[i].y > 0.0 ? attenuation[i].y : radius;
        if (dist > end)
          continue;

        float att = (end > start) ? (1.0 - smoothstep(start, end, dist)) : 1.0;
        vec3 ldir = normalize(L);
        float spot_mask = 1.0;
        if (spot_cos_outer_kind[i].y > 0.5)
        {
          vec3 forward = spot_dir_cos_inner[i].xyz;
          float cosTheta = dot(forward, -ldir);
          float ci = spot_dir_cos_inner[i].w;
          float co = spot_cos_outer_kind[i].x;
          if (cosTheta < co)
            continue;
          spot_mask = smoothstep(co, ci, cosTheta);
        }
        float ndotl2 = max(dot(N, ldir), 0.0);
        lDiffuse += color_intensity[i].xyz * (color_intensity[i].w * att * ndotl2 * spot_mask);
      }
    }
  }
  else
  {
    currColor = ambient_color + vertex_color;
    accumlatedLight = vec3(0.0f, 0.0f, 0.0f);
  }

  return clamp(material.rgb * (currColor + lDiffuse), 0.0, 1.0);
}

void main()
{

  float dist_from_camera = distance(camera, f_position);
  bool fog = FogColor_FogOn.w != 0 && !bool(flags & eWMOBatch_Unfogged);

  vec4 tex = get_tex_color(f_texcoord, tex_array0, int(tex0));
  vec4 tex_2 = get_tex_color(f_texcoord_2, tex_array1, int(tex1));

  float alpha_test = !bool(alpha_test_mode) ? -1.f : (alpha_test_mode < 2 ? 0.878431372 : 0.003921568);

  if(tex.a < alpha_test)
  {
    discard;
  }

  vec4 vertex_color = vec4(0., 0., 0., 1.f);
  vec3 light_color = vec3(1.);

  if(bool(flags & eWMOBatch_HasMOCV))
  {
    vertex_color = f_vertex_color;
  }


  // see: https://github.com/Deamon87/WebWowViewerCpp/blob/master/wowViewerLib/src/glsl/wmoShader.glsl
  if(wmo_batch_shader == 3) // Env
  {
    vec3 env = tex_2.rgb * tex.rgb;
    out_color = vec4(apply_lighting(tex.rgb) + env, 1.);
  }
  else if(wmo_batch_shader == 5) // EnvMetal
  {
    vec3 env = tex_2.rgb * tex.rgb * tex.a;
    out_color = vec4(apply_lighting(tex.rgb) + env, 1.);
  }
  else if(wmo_batch_shader == 6) // TwoLayerDiffuse
  {
    vec3 layer2 = mix(tex.rgb, tex_2.rgb, tex_2.a);
    out_color = vec4(apply_lighting(mix(layer2, tex.rgb, vertex_color.a)), 1.);
  }
  else if(wmo_batch_shader == 8) // TwoLayerTerrain (MapObjTwoLayerTerrain; vertex projects tex layer 1 onto world XZ)
  {
    vec3 layer2 = mix(tex.rgb, tex_2.rgb, tex_2.a);
    out_color = vec4(apply_lighting(mix(layer2, tex.rgb, vertex_color.a)), 1.);
  }
  else if (wmo_batch_shader == 21 || wmo_batch_shader == 23)
  {
    out_color = vec4(apply_lighting(tex_2.rgb), 1.);
  }
  else if (wmo_batch_shader == 16) // DiffuseTerrain (blend WMO with terrain)
  {
    vec3 wmo_albedo = tex.rgb;

    // Blend mask from vertex color alpha (often sourced from MOCV2 alpha).
    float blend = clamp(vertex_color.a, 0.0, 1.0);

    if (wmo_terrain_blend_enabled != 0 && terrain_blend_inv_size > 0.0 && blend > 0.0001)
    {
      vec2 uv = (f_position.xz - terrain_blend_origin_xz) * terrain_blend_inv_size;
      // Outside the baked terrain region: fall back to plain WMO.
      if (all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))))
      {
        vec3 terrain_albedo = texture(terrain_blend_color, uv).rgb;
        wmo_albedo = mix(wmo_albedo, terrain_albedo, blend);
      }
    }

    out_color = vec4(apply_lighting(wmo_albedo), 1.);
  }
  else // default shader, used for shader 0,1,2,4 (Diffuse, Specular, Metal, Opaque)
  {
    out_color = vec4(apply_lighting(tex.rgb), 1.);
  }

  if(fog)
  {
    float start = AmbientColor_FogEnd.w * DiffuseColor_FogStart.w;

    vec3 fogParams;
    fogParams.x = -(1.0 / (AmbientColor_FogEnd.w - start));
    fogParams.y = (1.0 / (AmbientColor_FogEnd.w - start)) * AmbientColor_FogEnd.w;
    fogParams.z = LightDir_FogRate.w;

    float f1 = (dist_from_camera * fogParams.x) + fogParams.y;
    float f2 = max(f1, 0.0);
    float f3 = pow(f2, fogParams.z);
    float f4 = min(f3, 1.0);

    float fogFactor = 1.0 - f4;

    out_color.rgb = mix(out_color.rgb, FogColor_FogOn.rgb, fogFactor);
  }

  if(out_color.a < alpha_test)
  {
    discard;
  }
}
