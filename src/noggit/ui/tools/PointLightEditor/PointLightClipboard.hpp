// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/World.h>

#include <optional>

namespace Noggit::Ui::Tools
{
  class PointLightClipboard final
  {
  public:
    [[nodiscard]] static bool hasLight();
    static void set(World::PointLight const& light);
    [[nodiscard]] static std::optional<World::PointLight> light();
    static void clear();

  private:
    static std::optional<World::PointLight> _stored;
  };
}
