// This file is part of Noggit3, licensed under GNU General Public License (version 3).
#version 330 core

in vec2 uv1;
in vec2 uv2;
in float camera_dist;
in vec3 norm;
in vec3 world_pos;

out vec4 out_color;

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

layout (std140) uniform modern_fog
{
  ivec4 mf_meta;
  vec4 fog_density_end_height;
  vec4 end_fog_color;
  vec4 fog_height_color_density;
  vec4 height_coeff_01;
  vec4 vfog_pos_radius[8];
  vec4 vfog_color_intensity[8];
};

uniform vec4 mesh_color;
uniform int blend_mode;

uniform sampler2DArray tex1;
uniform sampler2DArray tex2;
uniform int tex1_index;
uniform int tex2_index;

uniform int unfogged;
uniform int unlit;

uniform sampler2DShadow sun_shadow_depth;
uniform mat4 sun_shadow_matrix;
uniform vec3 sun_shadow_light_dir;
uniform float shadow_darkness;
uniform int realtime_shadows_enabled;

uniform int pixel_shader;

float sample_gpu_sun_shadow(vec3 world_pos, vec3 world_normal)
{
  if (realtime_shadows_enabled == 0)
  {
    return 1.0;
  }

  vec4 light = sun_shadow_matrix * vec4(world_pos, 1.0);
  vec3 proj = light.xyz / light.w;
  if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 || proj.z < 0.0 || proj.z > 1.0)
  {
    return 1.0;
  }

  vec3 light_dir = normalize(sun_shadow_light_dir);
  float ndotl = abs(dot(normalize(world_normal), light_dir));
  float bias = max(0.00012, 0.0012 * (1.0 - ndotl));
  float ref_depth = proj.z - bias;

  vec2 texel = 1.0 / vec2(textureSize(sun_shadow_depth, 0));
  vec2 uv = proj.xy;

  float lit = 0.0;
  for (int y = -1; y <= 1; ++y)
  {
    for (int x = -1; x <= 1; ++x)
    {
      lit += texture(sun_shadow_depth, vec3(uv + vec2(x, y) * texel, ref_depth));
    }
  }
  lit *= 0.1111111111111111;

  return mix(shadow_darkness, 1.0, lit);
}

void main()
{

  float alpha_test;
  int fog_mode;

  switch (blend_mode)
  {
      default:
      case 0: // Opaque
      {
          alpha_test = -1.0;
          fog_mode = 1;
          break;
      }
      case 1: // Alpha_key
      {
          alpha_test = (224.f / 255.f) * mesh_color.w;
          fog_mode = 1;
          break;
      }
      case 2: // Alpha
      {
          alpha_test = (1.f / 255.f) * mesh_color.w;
          fog_mode = 1;
          break;
      }
      case 3: // No_Add_Alpha
      case 4: // Add
      {
          alpha_test = (1.f / 255.f) * mesh_color.w;
          fog_mode = 2; // Warning: wiki is unsure on that for No_Add_Alpha
          break;
      }
      case 5: // Mod
      {
          alpha_test = (1.f / 255.f) * mesh_color.w;
          fog_mode = 3;
          break;
      }
      case 6: // Mod2X
      {
          alpha_test = (1.f / 255.f) * mesh_color.w;
          fog_mode = 4;
          break;
      }
  }

  vec4 color = vec4(0.0);

  if(mesh_color.a < alpha_test)
  {
    discard;
  }
  
  // code from Deamon87 and https://wowdev.wiki/M2/Rendering#Pixel_Shaders
  if (pixel_shader == 0) //Combiners_Opaque
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      color.rgb = texture1.rgb * mesh_color.rgb;
      color.a = mesh_color.a;
  } 
  else if (pixel_shader == 1) // Combiners_Decal
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      color.rgb = mix(mesh_color.rgb, texture1.rgb, mesh_color.a);
      color.a = mesh_color.a;
  } 
  else if (pixel_shader == 2) // Combiners_Add
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      color.rgba = texture1.rgba + mesh_color.rgba;
  } 
  else if (pixel_shader == 3) // Combiners_Mod2x
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      color.rgb = texture1.rgb * mesh_color.rgb * vec3(2.0);
      color.a = texture1.a * mesh_color.a * 2.0;
  } 
  else if (pixel_shader == 4) // Combiners_Fade
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      color.rgb = mix(texture1.rgb, mesh_color.rgb, mesh_color.a);
      color.a = mesh_color.a;
  } 
  else if (pixel_shader == 5) // Combiners_Mod
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      color.rgba = texture1.rgba * mesh_color.rgba;
  } 
  else if (pixel_shader == 6) // Combiners_Opaque_Opaque
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      vec4 texture2 = texture(tex2, vec3(uv2, tex2_index));
      color.rgb = texture1.rgb * texture2.rgb * mesh_color.rgb;
      color.a = mesh_color.a;
  } 
  else if (pixel_shader == 7) // Combiners_Opaque_Add
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      vec4 texture2 = texture(tex2, vec3(uv2, tex2_index));
      color.rgb = texture2.rgb + texture1.rgb * mesh_color.rgb;
      color.a = mesh_color.a + texture1.a;
  } 
  else if (pixel_shader == 8) // Combiners_Opaque_Mod2x
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      vec4 texture2 = texture(tex2, vec3(uv2, tex2_index));
      color.rgb = texture1.rgb * mesh_color.rgb * texture2.rgb * vec3(2.0);
      color.a  = texture2.a * mesh_color.a * 2.0;
  } 
  else if (pixel_shader == 9)  // Combiners_Opaque_Mod2xNA
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      vec4 texture2 = texture(tex2, vec3(uv2, tex2_index));
      color.rgb = texture1.rgb * mesh_color.rgb * texture2.rgb * vec3(2.0);
      color.a  = mesh_color.a;
  } 
  else if (pixel_shader == 10) // Combiners_Opaque_AddNA
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      vec4 texture2 = texture(tex2, vec3(uv2, tex2_index));
      color.rgb = texture2.rgb + texture1.rgb * mesh_color.rgb;
      color.a = mesh_color.a;
  } 
  else if (pixel_shader == 11) // Combiners_Opaque_Mod
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      vec4 texture2 = texture(tex2, vec3(uv2, tex2_index));
      color.rgb = texture1.rgb * texture2.rgb * mesh_color.rgb;
      color.a = texture2.a * mesh_color.a;
  } 
  else if (pixel_shader == 12) // Combiners_Mod_Opaque
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      vec4 texture2 = texture(tex2, vec3(uv2, tex2_index));
      color.rgb = texture1.rgb * texture2.rgb * mesh_color.rgb;
      color.a = texture1.a;
  } 
  else if (pixel_shader == 13) // Combiners_Mod_Add
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      vec4 texture2 = texture(tex2, vec3(uv2, tex2_index));
      color.rgba = texture2.rgba + texture1.rgba * mesh_color.rgba;
  } 
  else if (pixel_shader == 14) // Combiners_Mod_Mod2x
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      vec4 texture2 = texture(tex2, vec3(uv2, tex2_index));
      color.rgba = texture1.rgba * texture2.rgba * mesh_color.rgba * vec4(2.0);
  } 
  else if (pixel_shader == 15) // Combiners_Mod_Mod2xNA
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      vec4 texture2 = texture(tex2, vec3(uv2, tex2_index));
      color.rgb = texture1.rgb * texture2.rgb * mesh_color.rgb * vec3(2.0);
      color.a = texture1.a * mesh_color.a;
  } 
  else if (pixel_shader == 16) // Combiners_Mod_AddNA
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      vec4 texture2 = texture(tex2, vec3(uv2, tex2_index));
      color.rgb = texture2.rgb + texture1.rgb * mesh_color.rgb;
      color.a = texture1.a * mesh_color.a;
  } 
  else if (pixel_shader == 17) // Combiners_Mod_Mod
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      vec4 texture2 = texture(tex2, vec3(uv2, tex2_index));
      color.rgba = texture1.rgba * texture2.rgba * mesh_color.rgba;
  } 
  else if (pixel_shader == 18) // Combiners_Add_Mod
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      vec4 texture2 = texture(tex2, vec3(uv2, tex2_index));
      color.rgb = (texture1.rgb + mesh_color.rgb) * texture2.a;
      color.a = (texture1.a + mesh_color.a) * texture2.a;
  } 
  else if (pixel_shader == 19) // Combiners_Mod2x_Mod2x
  {
      vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
      vec4 texture2 = texture(tex2, vec3(uv2, tex2_index));
      color.rgba = texture1.rgba * texture2.rgba * mesh_color.rgba * vec4(4.0);
  }
  else if (pixel_shader == 20)  // Combiners_Opaque_Mod2xNA_Alpha
  {
    vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
    vec4 texture2 = texture(tex2, vec3(uv2, tex2_index));
    color.rgb = (mesh_color.rgb * texture1.rgb) * mix(texture2.rgb * 2.0, vec3(1.0), texture1.a);
    color.a = mesh_color.a;
  }
  else if (pixel_shader == 21)   //Combiners_Opaque_AddAlpha
  {
    vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
    vec4 texture2 = texture(tex2, vec3(uv2, tex2_index));
    color.rgb = (mesh_color.rgb * texture1.rgb) + (texture2.rgb * texture2.a);
    color.a = mesh_color.a;
  }
  else if (pixel_shader == 22)   // Combiners_Opaque_AddAlpha_Alpha
  {
    vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
    vec4 texture2 = texture(tex2, vec3(uv2, tex2_index));
    color.rgb = (mesh_color.rgb * texture1.rgb) + (texture2.rgb * texture2.a * texture1.a);
    color.a = mesh_color.a;
  }

  if(color.a < alpha_test)
  {
    discard;
  }

#ifdef M2_SHADOW_DEPTH_PASS
  // Alpha_key + opaque combiner: final color.a ignores texture alpha; cut out from texture for shadows.
  if (blend_mode == 1 && alpha_test > 0.0)
  {
    vec4 texture1 = texture(tex1, vec3(uv1, tex1_index));
    if (texture1.a * mesh_color.w < alpha_test)
    {
      discard;
    }
  }
  return;
#endif

  // apply world lighting
  vec3 currColor;
  vec3 lDiffuse = vec3(0.0, 0.0, 0.0);
  vec3 accumlatedLight = vec3(1.0, 1.0, 1.0);
  float nDotL = clamp(dot(normalize(norm), -normalize(vec3(-LightDir_FogRate.x, LightDir_FogRate.z, -LightDir_FogRate.y))), 0.0, 1.0);

  if(unlit == 0)
  {

      vec3 ambientColor = AmbientColor_FogEnd.xyz;

      vec3 skyColor = (ambientColor * 1.10000002);
      vec3 groundColor = (ambientColor * 0.699999988);

      currColor = mix(groundColor, skyColor, 0.5 + (0.5 * nDotL));
      lDiffuse = DiffuseColor_FogStart.xyz * nDotL;

      if (meta.y != 0)
      {
        vec3 N = normalize(norm);
        for (int i = 0; i < meta.x; ++i)
        {
          vec3 L = position_radius[i].xyz - world_pos;
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
      currColor = AmbientColor_FogEnd.xyz;
      accumlatedLight = vec3(0.0f, 0.0f, 0.0f);
  }

  vec3 albedo = color.rgb;
  float shadow_mul = 1.0;
  if (realtime_shadows_enabled != 0 && unlit == 0)
  {
    shadow_mul = sample_gpu_sun_shadow(world_pos, norm);
  }
  color.rgb = clamp(albedo * currColor + albedo * lDiffuse * shadow_mul, 0.0, 1.0);

  if(FogColor_FogOn.w != 0 && unfogged == 0)
  {
    float start = DiffuseColor_FogStart.w;
    vec3 fogParams;
    fogParams.x = -(1.0 / (AmbientColor_FogEnd.w - start));
    fogParams.y = (1.0 / (AmbientColor_FogEnd.w - start)) * AmbientColor_FogEnd.w;
    fogParams.z = (mf_meta.x != 0 && fog_density_end_height.x > 0.0) ? fog_density_end_height.x : LightDir_FogRate.w;

    float f1 = (camera_dist * fogParams.x) + fogParams.y;
    float f2 = max(f1, 0.0);
    float f3 = pow(f2, fogParams.z);
    float fogFactor = 1.0 - min(f3, 1.0);

    vec3 fog = FogColor_FogOn.rgb;
    if(fog_mode == 2) fog = vec3(0.);
    else if(fog_mode == 3) fog = vec3(1.);
    else if(fog_mode == 4) fog = vec3(0.5);

    vec3 fogged = mix(color.rgb, fog, fogFactor);
    if (mf_meta.x != 0 && fog_density_end_height.y > 0.0)
    {
      float endT = clamp(camera_dist / fog_density_end_height.y, 0.0, 1.0);
      fogged = mix(fogged, end_fog_color.rgb, endT * fogFactor);
    }
    for (int i = 0; i < mf_meta.y; ++i)
    {
      float d = distance(vfog_pos_radius[i].xyz, world_pos);
      float r = max(vfog_pos_radius[i].w, 1.0);
      float vf = clamp(1.0 - d / r, 0.0, 1.0) * vfog_color_intensity[i].w;
      fogged = mix(fogged, vfog_color_intensity[i].rgb, vf * fogFactor);
    }
    color.rgb = fogged;
  }

  out_color = color;
}
