// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <glm/vec2.hpp>

#include <QJsonArray>
#include <QJsonObject>

#include <vector>

namespace Noggit
{
  /// Piecewise-linear radial falloff curve: x = normalized distance t in [0,1], y = multiplier in [0,1].
  class BrushFalloffCurve
  {
  public:
    BrushFalloffCurve();

    [[nodiscard]] std::vector<glm::vec2> const& controlPoints() const { return _points; }
    void setControlPoints(std::vector<glm::vec2> points);

    /// t: distance / outerRadius in [0,1+]; values >1 sample as 1.
    [[nodiscard]] float sample(float t) const;

    [[nodiscard]] bool enabled() const { return _enabled; }
    void setEnabled(bool v) { _enabled = v; }

    [[nodiscard]] QJsonObject toJson() const;
    void fromJson(QJsonObject const& o);

    /// Default half-disk friendly curve: strong at center, soft at edge.
    static BrushFalloffCurve makeDefault();

  private:
    void sortAndClampPoints();

    std::vector<glm::vec2> _points;
    bool _enabled = false;
  };
}
