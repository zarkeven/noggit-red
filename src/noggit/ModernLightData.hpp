// This file is part of Noggit3, licensed under GNU General Public License (version 3).
#pragma once

#include <glm/vec3.hpp>

#include <array>
#include <string>
#include <vector>

//! Must match NUM_SkyColorNames in Sky.h
inline constexpr int kModernLightColorCount = 18;
inline constexpr int kModernSkyParamCount = 8;

struct LightDataKeyframe
{
  int time = 0;
  std::array<glm::vec3, kModernLightColorCount> colors{};
  float fog_end = 6500.f;
  float fog_scaler = 0.1f;
  float fog_density = 0.f;
  float fog_height = 0.f;
  float fog_height_scaler = 0.f;
  float fog_height_density = 0.f;
  glm::vec3 end_fog_color{0.f};
  float end_fog_color_distance = 0.f;
  glm::vec3 sun_fog_color{0.f};
  float sun_fog_strength = 1.f;
  glm::vec3 fog_height_color{0.f};
  float fog_height_coeff[4]{};
  float main_fog_coeff[4]{};
  float cloud_density = 1.f;
};

struct ModernLightParamRecord
{
  int id = 0;
  bool highlight_sky = false;
  float river_shallow_alpha = 0.5f;
  float river_deep_alpha = 1.f;
  float ocean_shallow_alpha = 0.75f;
  float ocean_deep_alpha = 1.f;
  float glow = 0.5f;
  int skybox_id = 0;
  int skybox_flags = 0;
  std::vector<LightDataKeyframe> keyframes;
};

struct ModernLightRecord
{
  int id = 0;
  int map_id = 0;
  glm::vec3 pos{0.f};
  float r1 = 0.f;
  float r2 = 0.f;
  unsigned int sky_params[kModernSkyParamCount]{};
};

struct ModernLightSkyboxRecord
{
  int id = 0;
  std::string name;
  int flags = 0;
  int file_data_id = 0;
  int celestial_file_data_id = 0;
};
