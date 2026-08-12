// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/BrushFalloffCurve.hpp>

#include <QJsonObject>
#include <QtWidgets/QWidget>

class MapView;

namespace Noggit::Ui
{
  class TerrainTool;
  class flatten_blur_tool;
}

class QButtonGroup;
class QCheckBox;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;

namespace Noggit::Ui::Tools::UiCommon
{
  class ExtendedSlider;
}

namespace Noggit::Ui
{
  class TerrainUnifiedToolWidget final : public QWidget
  {
    Q_OBJECT

  public:
    enum class Family
    {
      Sculpt = 0,
      Surface = 1,
    };

    explicit TerrainUnifiedToolWidget(MapView* map_view, QWidget* parent = nullptr);

    [[nodiscard]] static QString radialPresetDirectory();

    [[nodiscard]] bool customRadialFalloffEnabled() const;
    [[nodiscard]] Noggit::BrushFalloffCurve const* activeRadialFalloff() const;
    void setActiveFamily(Family f);
    [[nodiscard]] Family activeFamily() const { return _family; }
    [[nodiscard]] Noggit::Ui::TerrainTool* sculptTool() const { return _terrainTool; }
    [[nodiscard]] Noggit::Ui::flatten_blur_tool* surfaceTool() const { return _flattenTool; }

    [[nodiscard]] QJsonObject toJSON() const;
    void fromJSON(QJsonObject const& json);

  signals:
    void activeFamilyChanged(int family);

  private slots:
    void onUnifiedRadiusChanged(double v);
    void onCustomFalloffToggled(int state);
    void onRadialPresetSelectionChanged();
    void onSaveRadialPresetClicked();
    void onImportRadialPresetClicked();
    void onOpenRadialPresetsFolderClicked();
    void onDeleteRadialPresetClicked();

  private:
    void buildUi();
    void applyUnifiedRadius(double v);
    void syncRadialFalloffToTools();
    void updateCurveInteractionState();
    void reloadCurveEditorFromCurve();
    void applyRadialCurveFromPresetItem(QListWidgetItem* item);
    void refreshRadialPresetList();

    MapView* _map_view = nullptr;
    Family _family = Family::Sculpt;

    QButtonGroup* _family_buttons = nullptr;
    Noggit::Ui::Tools::UiCommon::ExtendedSlider* _unified_radius = nullptr;
    QCheckBox* _custom_falloff_chk = nullptr;
    QWidget* _curve_editor_section = nullptr;
    QListWidget* _radial_preset_list = nullptr;
    QPushButton* _save_radial_preset_btn = nullptr;
    QPushButton* _import_radial_preset_btn = nullptr;
    QPushButton* _open_radial_presets_folder_btn = nullptr;
    QPushButton* _delete_radial_preset_btn = nullptr;
    QWidget* _curve_editor = nullptr;
    QStackedWidget* _stack = nullptr;
    Noggit::Ui::TerrainTool* _terrainTool = nullptr;
    Noggit::Ui::flatten_blur_tool* _flattenTool = nullptr;

    Noggit::BrushFalloffCurve _radial_curve;
  };
}
