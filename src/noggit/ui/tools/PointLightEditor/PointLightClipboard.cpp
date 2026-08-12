// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include "PointLightClipboard.hpp"

using namespace Noggit::Ui::Tools;

std::optional<World::PointLight> PointLightClipboard::_stored{};

bool PointLightClipboard::hasLight()
{
  return _stored.has_value();
}

void PointLightClipboard::set(World::PointLight const& light)
{
  _stored = light;
}

std::optional<World::PointLight> PointLightClipboard::light()
{
  return _stored;
}

void PointLightClipboard::clear()
{
  _stored.reset();
}
