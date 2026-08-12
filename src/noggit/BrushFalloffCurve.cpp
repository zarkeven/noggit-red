// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/BrushFalloffCurve.hpp>

#include <algorithm>
#include <cmath>

namespace Noggit
{
  BrushFalloffCurve::BrushFalloffCurve()
  {
    _points = { glm::vec2(0.f, 1.f), glm::vec2(1.f, 0.f) };
  }

  void BrushFalloffCurve::sortAndClampPoints()
  {
    for (auto& p : _points)
    {
      p.x = std::clamp(p.x, 0.f, 1.f);
      p.y = std::clamp(p.y, 0.f, 1.f);
    }

    std::sort(_points.begin(), _points.end(), [](glm::vec2 const& a, glm::vec2 const& b) {
      return a.x < b.x;
    });

    if (_points.size() < 2)
    {
      _points = { glm::vec2(0.f, 1.f), glm::vec2(1.f, 0.f) };
    }
  }

  void BrushFalloffCurve::setControlPoints(std::vector<glm::vec2> points)
  {
    _points = std::move(points);
    sortAndClampPoints();
  }

  float BrushFalloffCurve::sample(float t) const
  {
    if (_points.size() < 2)
    {
      return 1.f;
    }

    t = std::clamp(t, 0.f, 1.f);

    if (t <= _points.front().x)
    {
      return _points.front().y;
    }
    if (t >= _points.back().x)
    {
      return _points.back().y;
    }

    for (std::size_t i = 1; i < _points.size(); ++i)
    {
      glm::vec2 const& a = _points[i - 1];
      glm::vec2 const& b = _points[i];
      if (t <= b.x)
      {
        float const span = b.x - a.x;
        if (span <= 1e-6f)
        {
          return b.y;
        }
        float const u = (t - a.x) / span;
        return a.y + (b.y - a.y) * u;
      }
    }

    return _points.back().y;
  }

  QJsonObject BrushFalloffCurve::toJson() const
  {
    QJsonArray arr;
    for (glm::vec2 const& p : _points)
    {
      QJsonObject pt;
      pt.insert(QStringLiteral("x"), static_cast<double>(p.x));
      pt.insert(QStringLiteral("y"), static_cast<double>(p.y));
      arr.append(pt);
    }

    QJsonObject o;
    o.insert(QStringLiteral("points"), arr);
    o.insert(QStringLiteral("enabled"), _enabled);
    return o;
  }

  void BrushFalloffCurve::fromJson(QJsonObject const& o)
  {
    _enabled = o.value(QStringLiteral("enabled")).toBool(false);

    std::vector<glm::vec2> pts;
    QJsonArray const arr = o.value(QStringLiteral("points")).toArray();
    pts.reserve(static_cast<std::size_t>(arr.size()));
    for (QJsonValue const& v : arr)
    {
      QJsonObject const pt = v.toObject();
      pts.emplace_back(
        static_cast<float>(pt.value(QStringLiteral("x")).toDouble(0.0)),
        static_cast<float>(pt.value(QStringLiteral("y")).toDouble(0.0))
      );
    }

    setControlPoints(std::move(pts));
  }

  BrushFalloffCurve BrushFalloffCurve::makeDefault()
  {
    BrushFalloffCurve curve;
    curve.setControlPoints({ glm::vec2(0.f, 1.f), glm::vec2(0.55f, 0.65f), glm::vec2(1.f, 0.f) });
    return curve;
  }
}
