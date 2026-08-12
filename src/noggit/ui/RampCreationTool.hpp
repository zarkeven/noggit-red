// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <QtWidgets/QWidget>

class MapView;
class QPushButton;
class QLabel;
class QSlider;

namespace Noggit::Ui
{
  class RampCreationTool : public QWidget
  {
    Q_OBJECT

  public:
    explicit RampCreationTool(MapView* map_view, QWidget* parent = nullptr);

    [[nodiscard]] float radius() const;
    [[nodiscard]] float capLength() const;
    [[nodiscard]] float blendStrength() const; // 0 = full ramp apply, 1 = minimal change

    void clearPickedPoints();
    void refreshPointLabels();

  signals:
    void createRampRequested();

  private:
    void updatePickButtonHighlight();
    void updateCreateEnabled();

    MapView* _map_view;

    QPushButton* _pick_first = nullptr;
    QPushButton* _pick_second = nullptr;
    QPushButton* _create = nullptr;
    QLabel* _first_label = nullptr;
    QLabel* _second_label = nullptr;
    QSlider* _radius_slider = nullptr;
    QSlider* _cap_slider = nullptr;
    QSlider* _blend_slider = nullptr;
  };
}
