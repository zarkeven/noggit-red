// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/Brush.h>

#include <algorithm>
#include <cmath>

namespace
{
  constexpr float k_radius_epsilon = 1e-5f;
}

void Brush::updateFalloffBand()
{
  _inner_radius = std::clamp(_inner_radius, 0.f, _outer_radius);
}

void Brush::init()
{
  _outer_radius = 15.f;
  _inner_radius = 7.5f;
  updateFalloffBand();
}

void Brush::setInnerRadius(float inner)
{
  _inner_radius = std::max(0.f, inner);
  updateFalloffBand();
}

void Brush::setRadius(float outer)
{
  _outer_radius = std::max(0.f, outer);
  updateFalloffBand();
}

void Brush::setHardness(float hardness)
{
  hardness = std::clamp(hardness, 0.f, 1.f);
  _inner_radius = hardness * _outer_radius;
  updateFalloffBand();
}

float Brush::getInnerRadius() const
{
  return _inner_radius;
}

float Brush::getRadius() const
{
  return _outer_radius;
}

float Brush::getHardness() const
{
  if (_outer_radius <= k_radius_epsilon)
  {
    return 0.f;
  }
  return _inner_radius / _outer_radius;
}

float Brush::getValue(float dist) const
{
  if (dist > _outer_radius)
  {
    return 0.f;
  }
  if (dist <= _inner_radius)
  {
    return 1.f;
  }

  float const falloff_band = _outer_radius - _inner_radius;
  if (falloff_band <= k_radius_epsilon)
  {
    return 1.f;
  }

  return 1.f - (dist - _inner_radius) / falloff_band;
}
