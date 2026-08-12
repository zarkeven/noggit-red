// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/water_spline.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/spline.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace Noggit
{
  namespace
  {
    glm::vec3 cubicBezier(glm::vec3 const& p0, glm::vec3 const& p1, glm::vec3 const& p2, glm::vec3 const& p3, float t)
    {
      float const u = 1.f - t;
      float const uu = u * u;
      float const tt = t * t;
      return uu * u * p0 + 3.f * uu * t * p1 + 3.f * u * tt * p2 + tt * t * p3;
    }

    void appendSamplesAlongSegment
      ( glm::vec3 const& a
      , glm::vec3 const& b
      , float sample_spacing
      , std::vector<glm::vec3>& out
      , bool include_start
      )
    {
      glm::vec3 const delta = b - a;
      float const len = glm::length(delta);
      if (len < 1e-4f)
      {
        if (include_start || out.empty())
        {
          out.push_back(a);
        }
        return;
      }

      int const steps = std::max(1, static_cast<int>(std::ceil(len / std::max(sample_spacing, 0.25f))));
      for (int i = include_start ? 0 : 1; i <= steps; ++i)
      {
        float const t = static_cast<float>(i) / static_cast<float>(steps);
        out.push_back(a + delta * t);
      }
    }

    void sampleLinear(std::vector<glm::vec3> const& pts, float spacing, std::vector<glm::vec3>& out)
    {
      for (std::size_t i = 0; i + 1 < pts.size(); ++i)
      {
        appendSamplesAlongSegment(pts[i], pts[i + 1], spacing, out, i == 0);
      }
    }

    void sampleCatmullRom(std::vector<glm::vec3> const& pts, float spacing, std::vector<glm::vec3>& out)
    {
      if (pts.size() < 2)
      {
        return;
      }
      if (pts.size() == 2)
      {
        sampleLinear(pts, spacing, out);
        return;
      }

      for (std::size_t i = 0; i + 1 < pts.size(); ++i)
      {
        glm::vec3 const& p0 = pts[i == 0 ? 0 : i - 1];
        glm::vec3 const& p1 = pts[i];
        glm::vec3 const& p2 = pts[i + 1];
        glm::vec3 const& p3 = pts[i + 2 < pts.size() ? i + 2 : pts.size() - 1];

        float const seg_len = glm::length(p2 - p1);
        int const steps = std::max(1, static_cast<int>(std::ceil(seg_len / std::max(spacing, 0.25f))));
        for (int s = (i == 0 ? 0 : 1); s <= steps; ++s)
        {
          float const t = static_cast<float>(s) / static_cast<float>(steps);
          out.push_back(glm::catmullRom(p0, p1, p2, p3, t));
        }
      }
    }

    void sampleCubicBezier(std::vector<glm::vec3> const& pts, float spacing, std::vector<glm::vec3>& out)
    {
      if (pts.size() < 2)
      {
        return;
      }
      if (pts.size() == 2)
      {
        sampleLinear(pts, spacing, out);
        return;
      }

      for (std::size_t i = 0; i + 1 < pts.size(); ++i)
      {
        glm::vec3 const& p0 = pts[i];
        glm::vec3 const& p3 = pts[i + 1];
        glm::vec3 const prev = pts[i == 0 ? 0 : i - 1];
        glm::vec3 const next = pts[i + 2 < pts.size() ? i + 2 : pts.size() - 1];

        // Auto handles: 1/3 along neighboring tangents (smooth cubic through anchors).
        glm::vec3 const c1 = p0 + (p3 - prev) / 6.f;
        glm::vec3 const c2 = p3 - (next - p0) / 6.f;

        float const seg_len = glm::length(p3 - p0);
        int const steps = std::max(1, static_cast<int>(std::ceil(seg_len / std::max(spacing, 0.25f))));
        for (int s = (i == 0 ? 0 : 1); s <= steps; ++s)
        {
          float const t = static_cast<float>(s) / static_cast<float>(steps);
          out.push_back(cubicBezier(p0, c1, c2, p3, t));
        }
      }
    }
  }

  std::vector<glm::vec3> sampleWaterSpline
    ( std::vector<glm::vec3> const& control_points
    , WaterCurveType curve_type
    , float sample_spacing
    )
  {
    std::vector<glm::vec3> out;
    if (control_points.empty())
    {
      return out;
    }
    if (control_points.size() == 1)
    {
      out.push_back(control_points.front());
      return out;
    }

    out.reserve(control_points.size() * 8);
    switch (curve_type)
    {
    case WaterCurveType::Linear:
      sampleLinear(control_points, sample_spacing, out);
      break;
    case WaterCurveType::CubicBezier:
      sampleCubicBezier(control_points, sample_spacing, out);
      break;
    case WaterCurveType::CatmullRom:
      sampleCatmullRom(control_points, sample_spacing, out);
      break;
    }
    return out;
  }

  void waterSplineCorridorEdges
    ( std::vector<glm::vec3> const& centerline
    , float half_width
    , std::vector<glm::vec3>& out_left
    , std::vector<glm::vec3>& out_right
    )
  {
    out_left.clear();
    out_right.clear();
    if (centerline.size() < 2 || half_width <= 0.f)
    {
      return;
    }

    out_left.reserve(centerline.size());
    out_right.reserve(centerline.size());

    for (std::size_t i = 0; i < centerline.size(); ++i)
    {
      glm::vec3 tangent(0.f);
      if (i + 1 < centerline.size())
      {
        tangent += centerline[i + 1] - centerline[i];
      }
      if (i > 0)
      {
        tangent += centerline[i] - centerline[i - 1];
      }
      tangent.y = 0.f;
      float const len = glm::length(tangent);
      if (len < 1e-4f)
      {
        tangent = glm::vec3(1.f, 0.f, 0.f);
      }
      else
      {
        tangent /= len;
      }

      glm::vec3 const side(-tangent.z * half_width, 0.f, tangent.x * half_width);
      out_left.push_back(centerline[i] + side);
      out_right.push_back(centerline[i] - side);
    }
  }
}
