// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

class Brush
{
private:
  float _inner_radius = 0.f;
  float _outer_radius = 0.f;

  void updateFalloffBand();

public:
  void setInnerRadius(float inner);
  void setRadius(float outer);
  /// Legacy: inner = hardness * outer radius (hardness in [0, 1]).
  void setHardness(float hardness);
  float getInnerRadius() const;
  float getRadius() const;
  /// Legacy: inner / outer when outer > 0, else 0.
  float getHardness() const;
  float getValue(float dist) const;
  void init();
};
