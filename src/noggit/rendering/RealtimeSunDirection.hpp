// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <glm/vec3.hpp>

namespace Noggit::Rendering
{
  //! dayDir is incoming light in WoW Z-up; swizzle to Noggit Y-up and flip Z so the sun stays above the map.
  inline glm::vec3 wow_directional_light_toward_sun (glm::vec3 const& light_dir_ubo)
  {
    glm::vec3 const to_sun (light_dir_ubo.x, -light_dir_ubo.z, light_dir_ubo.y);
    float const len = glm::length (to_sun);
    if (len < 1e-4f)
    {
      return glm::vec3 (0.f, 1.f, 0.f);
    }
    return to_sun / len;
  }

  inline glm::vec3 editor_realtime_shadow_sun()
  {
    return glm::normalize (glm::vec3 (1.f, 1.f, 1.f));
  }
}
