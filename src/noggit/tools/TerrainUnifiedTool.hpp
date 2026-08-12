// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/Tool.hpp>

namespace Noggit
{
  namespace Ui
  {
    class TerrainUnifiedToolWidget;
  }

  class TerrainUnifiedTool final : public Tool
  {
  public:
    explicit TerrainUnifiedTool(MapView* mapView);
    ~TerrainUnifiedTool() override;

    [[nodiscard]] char const* name() const override;
    [[nodiscard]] editing_mode editingMode() const override;
    [[nodiscard]] Ui::FontNoggit::Icons icon() const override;

    void setupUi(Ui::Tools::ToolPanel* toolPanel) override;
    void postUiSetup() override;

    [[nodiscard]] ToolDrawParameters drawParameters() const override;

    void onSelected() override;
    void onTick(float deltaTime, TickParameters const& params) override;
    void onMouseMove(MouseMoveParameters const& params) override;
    void onMouseWheel(MouseWheelParameters const& params) override;

    void setUnifiedFamilyToSculpt();
    void setUnifiedFamilyToSurface();

  private:
    Ui::TerrainUnifiedToolWidget* _widget = nullptr;
  };
}
