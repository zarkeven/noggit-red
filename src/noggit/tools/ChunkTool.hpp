// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/Tool.hpp>

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

        void setupUi(Ui::Tools::ToolPanel* toolPanel) override;

        [[nodiscard]]
        unsigned int actionModality() const override;

        [[nodiscard]]
        ToolDrawParameters drawParameters() const override;

        [[nodiscard]]
        float brushRadius() const override;

        void onTick(float deltaTime, TickParameters const& params) override;

        void onMouseMove(MouseMoveParameters const& params) override;

        void onMouseRelease(MouseReleaseParameters const& params) override;

    private:
        void setupHotkeys();
        void paintChunkSelection(TickParameters const& params);
        void finishSelectionPaint();

        Ui::Tools::ChunkManipulator::ChunkManipulatorPanel* _chunkManipulator = nullptr;
        float _select_radius = 80.f;
        bool _selection_paint_active = false;
    };
}
