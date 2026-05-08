// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/Tool.hpp>

#include <QtGui/QIcon>

namespace Noggit
{
    namespace Ui::Tools::ChunkManipulator
    {
        class ChunkManipulatorPanel;
    }

    class ChunkTool final : public Tool
    {
    public:
        ChunkTool(MapView* mapView);
        ~ChunkTool();

        [[nodiscard]]
        virtual char const* name() const override;

        [[nodiscard]]
        virtual editing_mode editingMode() const override;

        [[nodiscard]]
        virtual Ui::FontNoggit::Icons icon() const override;

        [[nodiscard]]
        QIcon toolbarIconOverride() const override;

        void setupUi(Ui::Tools::ToolPanel* toolPanel) override;

        [[nodiscard]]
        unsigned int actionModality() const override;

        [[nodiscard]]
        ToolDrawParameters drawParameters() const override;

        [[nodiscard]]
        float brushRadius() const override;

        [[nodiscard]]
        float selectRadius() const { return _select_radius; }
        void setSelectRadius(float r);

        void onSelected() override;
        void onDeselected() override;

        void onTick(float deltaTime, TickParameters const& params) override;

        void onMouseRelease(MouseReleaseParameters const& params) override;

        void onMouseMove(MouseMoveParameters const& params) override;

    private:
        void setupHotkeys();
        void copySelectionToClipboard();

        Ui::Tools::ChunkManipulator::ChunkManipulatorPanel* _chunkManipulator = nullptr;
        float _select_radius = 80.f;
        /// True after at least one Shift+LMB add-to-selection paint this stroke (until copy or cancel).
        bool _painted_select_add = false;
        /// True after at least one Ctrl+LMB deselect paint this stroke (until copy or cancel).
        bool _painted_select_remove = false;
        bool _prev_shift_down = false;
        bool _prev_ctrl_down = false;
    };
}
