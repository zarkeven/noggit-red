// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/rendering/PointLightFlicker.hpp>

#include <FastNoise/FastNoise.h>

#include <algorithm>
#include <cmath>

namespace Noggit::Rendering
{
namespace
{
  FastNoise::SmartNode<const FastNoise::OpenSimplex2> flicker_noise()
  {
    static auto const n = FastNoise::New<FastNoise::OpenSimplex2>();
    return n;
  }

  constexpr float k_pi = 3.14159265358979323846f;
}

float point_light_intensity_multiplier (World::PointLight const& light, float time_seconds)
{
  if (light.flicker_mode == 0)
    return 1.f;

  float const amp = std::clamp (light.flicker_intensity / 100.f, 0.f, 1.f);
  float const speed = std::max (0.001f, light.flicker_speed);
  int const seed = static_cast<int>(light.flicker_seed | 1u);

  // Phase scaled so JSON-ish speeds (e.g. 15 vs 150) produce clearly different rates.
  float const phase = time_seconds * (speed / 15.f);

  float wiggle = 0.f;
  switch (light.flicker_mode)
  {
    case 1: // sine (smooth)
      wiggle = std::sin (phase * k_pi * 0.5f);
      break;
    case 2: // noise (smooth) — FastNoise2 OpenSimplex2 ~ [-1, 1]
    {
      float const x = phase * 1.85f;
      float const y = static_cast<float>(light.id & 255) * 0.04f
                      + static_cast<float>(seed & 0xFF) * 0.001f;
      wiggle = flicker_noise()->GenSingle2D (x, y, seed);
      break;
    }
    case 3: // harsh step from noise sample
    {
      float const x = phase * 1.85f;
      float const y = static_cast<float>((seed ^ (light.id * 0x9E3779B9u)) & 0xFFFFu) * 0.001f;
      float const n = flicker_noise()->GenSingle2D (x, y, seed ^ 0x6A09E667u);
      wiggle = (n >= 0.f ? 1.f : -1.f);
      break;
    }
    default:
      return 1.f;
  }

  return std::clamp (1.f + amp * wiggle, 0.05f, 8.f);
}

}
