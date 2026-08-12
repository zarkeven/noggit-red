// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/Tool.hpp>

class MapView;

namespace Noggit
{
  namespace Ui::Tools
  {
    class PointLightEditor;
    class PointLightPropertyDialog;
    class ToolPanel;
  }

  class PointLightTool final : public Tool
  {
  public:
    explicit PointLightTool(::MapView* mapView);
    ~PointLightTool() override;

    [[nodiscard]]
    char const* name() const override;

    [[nodiscard]]
    editing_mode editingMode() const override;

    [[nodiscard]]
    Ui::FontNoggit::Icons icon() const override;

    void setupUi(Ui::Tools::ToolPanel* toolPanel) override;

    void onSelected() override;
    void onDeselected() override;

    void closePropertyDialogs();

    Ui::Tools::PointLightEditor* editor() const { return _editor; }

    void registerContextMenuItems(QMenu* menu) override;

  private:
    void setupHotkeys();
    Ui::Tools::PointLightEditor* _editor = nullptr;
    Ui::Tools::PointLightPropertyDialog* _property_dialog = nullptr;

    friend class Ui::Tools::PointLightEditor;
  };
}
