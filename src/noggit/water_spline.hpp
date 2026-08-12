// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <glm/vec3.hpp>

#include <vector>

namespace Noggit
{
  enum class WaterCurveType
  {
    Linear = 0,
    CubicBezier = 1,
    CatmullRom = 2,
  };

  //! Sample an open path of control anchors into dense world-space points.
  //! All curve types take the same user anchors; Bezier uses auto-generated handles.
  [[nodiscard]]
  std::vector<glm::vec3> sampleWaterSpline
    ( std::vector<glm::vec3> const& control_points
    , WaterCurveType curve_type
    , float sample_spacing = 2.f
    );

  //! Build left/right corridor edge polylines for preview (XZ-perpendicular offset).
  void waterSplineCorridorEdges
    ( std::vector<glm::vec3> const& centerline
    , float half_width
    , std::vector<glm::vec3>& out_left
    , std::vector<glm::vec3>& out_right
    );
}
