// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include "ChunkTool.hpp"

#include <noggit/ActionManager.hpp>
#include <noggit/Input.hpp>
#include <noggit/MapView.h>
#include <noggit/StringHash.hpp>
#include <noggit/Action.hpp>
#include <noggit/MapHeaders.h>
#include <noggit/ui/tools/ChunkManipulator/ChunkClipboard.hpp>
#include <noggit/ui/tools/ChunkManipulator/ChunkManipulatorPanel.hpp>
#include <noggit/ui/tools/ToolPanel/ToolPanel.hpp>
#include <noggit/World.h>

#include <opengl/context.inl>

#include <algorithm>

#include <QtCore/QSettings>
#include <QtCore/QString>

namespace Noggit
{
    ChunkTool::ChunkTool(MapView* mapView)
        : Tool{ mapView }
    {
        setupHotkeys();
    }

    ChunkTool::~ChunkTool()
    {
        delete _chunkManipulator;
    }

    void ChunkTool::setupHotkeys()
    {
        auto* mv = mapView();

        addHotkey("copySelection"_hash, Hotkey{
            .onPress = [=] {
                if (auto* clip = mv->getWorld()->chunkClipboard())
                    clip->copySelected(mv->cursorPosition());
                mv->invalidate();
            },
            .condition = [=] { return mv->get_editing_mode() == editing_mode::chunk && !NOGGIT_CUR_ACTION; },
            });

        addHotkey("chunkManipulatorPaste"_hash, Hotkey{
            .onPress = [=] {
                auto* clip = mv->getWorld()->chunkClipboard();
                if (!clip || !clip->hasCachedCopy())
                    return;
                mv->makeCurrent();
                OpenGL::context::scoped_setter const _(::gl, mv->context());

                int flags = ActionFlags::eNO_FLAG;
                if (clip->hasCachedCopy())
                {
                    flags |= ActionFlags::eCHUNKS_TERRAIN
                        | ActionFlags::eCHUNKS_VERTEX_COLOR
                        | ActionFlags::eCHUNK_SHADOWS
                        | ActionFlags::eCHUNKS_HOLES
                        | ActionFlags::eCHUNKS_AREAID
                        | ActionFlags::eCHUNKS_FLAGS
                        | ActionFlags::eCHUNKS_WATER
                        | ActionFlags::eCHUNKS_TEXTURE
                        | ActionFlags::eCHUNKS_SOUND_EMITTERS
                        | ActionFlags::ePOINT_LIGHTS_CHANGED;
                }

                if (Action* action = NOGGIT_ACTION_MGR->beginAction(mv, flags, ActionModalityControllers::eNONE))
                {
                    clip->pasteSelection(
                      mv->cursorPosition(),
                      static_cast<Noggit::Ui::Tools::ChunkManipulator::ChunkPasteFlags>(0),
                      action);

                    QString map_key = QString::fromStdString(mv->getWorld()->basename);
                    map_key.replace(QLatin1Char('/'), QLatin1Char('_'));
                    QString const fix_key = QStringLiteral("maps/%1/chunk_manipulator/fix_gaps").arg(map_key);
                    if (mv->settings()->value(fix_key, true).toBool())
                    {
                        mv->getWorld()->fixAllGaps();
                    }

                    NOGGIT_ACTION_MGR->endAction();
                }
                mv->invalidate();
            },
            .condition = [=] { return mv->get_editing_mode() == editing_mode::chunk && !NOGGIT_CUR_ACTION; },
            });
    }

    char const* ChunkTool::name() const
    {
        return "Chunk Manipulator";
    }

    editing_mode ChunkTool::editingMode() const
    {
        return editing_mode::chunk;
    }

    Ui::FontNoggit::Icons ChunkTool::icon() const
    {
        return Ui::FontNoggit::INFO;
    }

    void ChunkTool::setupUi(Ui::Tools::ToolPanel* toolPanel)
    {
        _chunkManipulator = new Noggit::Ui::Tools::ChunkManipulator::ChunkManipulatorPanel(mapView(), mapView());
        toolPanel->registerTool(this, _chunkManipulator);
    }

    unsigned int ChunkTool::actionModality() const
    {
        return 0;
    }

    ToolDrawParameters ChunkTool::drawParameters() const
    {
        return ToolDrawParameters{
            .radius = _select_radius,
            .inner_radius = 0.f,
            .cursor_type = CursorType::CIRCLE,
            .cursor_color = { 0.25f, 0.95f, 1.f, 0.75f },
        };
    }

    float ChunkTool::brushRadius() const
    {
        return _select_radius;
    }

    void ChunkTool::finishSelectionPaint()
    {
        if (!_selection_paint_active)
            return;

        _selection_paint_active = false;

        auto* mv = mapView();
        auto* clip = mv->getWorld()->chunkClipboard();
        if (!clip)
            return;

        if (!clip->selectedChunks().empty())
        {
            clip->copySelected(mv->cursorPosition());
        }

        mv->invalidate();
    }

    void ChunkTool::paintChunkSelection(TickParameters const& params)
    {
        auto* mv = mapView();
        auto* clip = mv->getWorld()->chunkClipboard();
        if (!clip)
            return;

        _selection_paint_active = true;

        auto const mode = (params.mod_shift_down && !params.mod_ctrl_down)
                            ? Noggit::Ui::Tools::ChunkManipulator::ChunkSelectionMode::SELECT
                            : Noggit::Ui::Tools::ChunkManipulator::ChunkSelectionMode::DESELECT;

        int const modality = (params.mod_shift_down ? ActionModalityControllers::eSHIFT : 0)
                           | (params.mod_ctrl_down ? ActionModalityControllers::eCTRL : 0)
                           | ActionModalityControllers::eLMB;

        if (NOGGIT_ACTION_MGR->beginAction(mv, ActionFlags::eNO_FLAG | ActionFlags::eDO_NOT_WRITE_HISTORY, modality))
        {
            clip->selectRange(mv->cursorPosition(), _select_radius, mode);
        }

        mv->invalidate();
    }

    void ChunkTool::onTick(float deltaTime, TickParameters const& params)
    {
        (void)deltaTime;
        auto* mv = mapView();
        if (params.displayMode != display_mode::in_3D || params.underMap)
            return;

        if (!params.left_mouse)
            return;

        if (params.mod_shift_down && !params.mod_ctrl_down && !params.mod_alt_down)
        {
            paintChunkSelection(params);
        }
        else if (params.mod_ctrl_down && !params.mod_shift_down && !params.mod_alt_down)
        {
            paintChunkSelection(params);
        }
    }

    void ChunkTool::onMouseMove(MouseMoveParameters const& params)
    {
        if (!params.mod_alt_down || params.mod_shift_down || params.mod_ctrl_down)
            return;
        if (!params.right_mouse)
            return;

        _select_radius = std::max(CHUNKSIZE * 0.25f, _select_radius + static_cast<float>(params.relative_movement.dx()) / 3.f);
        mapView()->invalidate();
    }

    void ChunkTool::onMouseRelease(MouseReleaseParameters const& params)
    {
        if (params.button == Qt::MouseButton::LeftButton)
        {
            finishSelectionPaint();
        }
        Tool::onMouseRelease(params);
    }
}
