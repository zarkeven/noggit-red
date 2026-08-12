// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/World.h>

#include <QWidget>

#include <QColor>

#include <optional>

class MapView;
class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
class QLabel;

namespace color_widgets
{
  class ColorSelector;
  class ColorWheel;
}

namespace Noggit
{
  class PointLightTool;
}

namespace Noggit::Ui::Tools
{
  class PointLightPropertyDialog final : public QWidget
  {
    Q_OBJECT

  public:
    PointLightPropertyDialog(Noggit::PointLightTool* tool
                            , ::MapView* mapView
                            , std::optional<std::size_t> light_index
                            , QWidget* parent = nullptr);

  signals:
    void dialogClosed();

  private:
    void buildUi();
    void loadFromWorkingCopy();
    void applyFieldsFromUi();
    void applyWorkingCopyToWorld();
    void deleteCurrentLight();
    void syncSpotControlsVisibility();
    void rebuildFlickerTypeCombo();
    void syncFlickerComboFromWorkingCopy();
    void syncFlickerTypeToWorkingCopy();
    void syncFlickerControlsVisibility();
    void updateFlickerSourceLabel();
    [[nodiscard]] int customFlickerComboIndex() const;
    void touchUndo();
    void onColorChanged(QColor const& color);

    void closeEvent(QCloseEvent* event) override;

    Noggit::PointLightTool* _tool = nullptr;
    ::MapView* _mapView = nullptr;
    std::optional<std::size_t> _light_index;
    World::PointLight _working{};

    color_widgets::ColorSelector* _color_selector = nullptr;
    color_widgets::ColorWheel* _color_wheel = nullptr;
    QDoubleSpinBox* _radius_spin = nullptr;
    QDoubleSpinBox* _atten_start_spin = nullptr;
    QDoubleSpinBox* _atten_end_spin = nullptr;
    QDoubleSpinBox* _flicker_speed_spin = nullptr;
    QDoubleSpinBox* _flicker_intensity_spin = nullptr;
    QDoubleSpinBox* _inner_angle_spin = nullptr;
    QDoubleSpinBox* _outer_angle_spin = nullptr;
    QDoubleSpinBox* _spot_radius_spin = nullptr;
    QSpinBox* _cookie_fdid_spin = nullptr;
    QSpinBox* _ngpl_cap_spin = nullptr;
    QLabel* _ngpl_status_lbl = nullptr;
    QComboBox* _light_type_combo = nullptr;
    QComboBox* _flicker_type_combo = nullptr;
    QLabel* _flicker_source_lbl = nullptr;
    QWidget* _spot_panel = nullptr;
    QLabel* _cookie_preview_lbl = nullptr;
  };
}
