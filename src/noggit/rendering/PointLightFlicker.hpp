// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/World.h>

namespace Noggit::Rendering
{
  // Runtime multiplier for authored `PointLight::intensity` (1 = no flicker).
  float point_light_intensity_multiplier (World::PointLight const& light, float time_seconds);
}
