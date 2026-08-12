// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <glm/mat4x4.hpp>

class Model;
class WMOInstance;

struct SunOccluderInstance
{
  Model* model = nullptr;
  glm::mat4x4 model_transform{1.f};
  WMOInstance* wmo = nullptr;
};
