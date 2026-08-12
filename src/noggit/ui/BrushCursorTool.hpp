// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/tool_enums.hpp>

#include <QtWidgets/QWidget>

class MapView;
class QButtonGroup;
class QLabel;

namespace color_widgets
{
  class ColorSelector;
}

namespace Noggit::Ui
{
  class BrushCursorTool : public QWidget
  {
    Q_OBJECT

  public:
    explicit BrushCursorTool(MapView* map_view, QWidget* parent = nullptr);

    void syncFromMapView();

  private:
    void applyStyle(BrushCursorStyle style);
    void applyOutlineColors();

    MapView* _map_view;

    QButtonGroup* _style_group = nullptr;
    QLabel* _outline_colors_label = nullptr;
    color_widgets::ColorSelector* _inner_color = nullptr;
    color_widgets::ColorSelector* _outer_color = nullptr;
    bool _syncing = false;
  };
}
