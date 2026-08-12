// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/Tool.hpp>

class MapView;

namespace Noggit
{
  namespace Ui::Tools
  {
    class SoundEmitterEditor;
    class ToolPanel;
  }

  class SoundEmitterTool final : public Tool
  {
  public:
    explicit SoundEmitterTool(::MapView* mapView);
    ~SoundEmitterTool() override;

    [[nodiscard]]
    char const* name() const override;

    [[nodiscard]]
    editing_mode editingMode() const override;

    [[nodiscard]]
    Ui::FontNoggit::Icons icon() const override;

    [[nodiscard]]
    QIcon toolbarIconOverride() const override;

    void setupUi(Ui::Tools::ToolPanel* toolPanel) override;

    void onSelected() override;
    void onDeselected() override;

  private:
    Ui::Tools::SoundEmitterEditor* _editor = nullptr;
  };
}
