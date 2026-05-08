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

#include <algorithm>

#include <QtCore/QSettings>
#include <QtCore/QString>
#include <QtGui/QIcon>

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

        addHotkey("chunkManipulatorPaste"_hash, Hotkey{
            .onPress = [=] {
                auto* clip = mv->getWorld()->chunkClipboard();
                if (!clip || !clip->hasCachedCopy())
                    return;
                mv->makeCurrent();
                OpenGL::context::scoped_setter const _(::gl, mv->context());

                int const flags = ActionFlags::eCHUNKS_TERRAIN
                    | ActionFlags::eCHUNKS_VERTEX_COLOR
                    | ActionFlags::eCHUNK_SHADOWS
                    | ActionFlags::eCHUNKS_HOLES
                    | ActionFlags::eCHUNKS_AREAID
                    | ActionFlags::eCHUNKS_FLAGS;

                if (Action* action = NOGGIT_ACTION_MGR->beginAction(mv, flags, ActionModalityControllers::eNONE))
                {
                    (void)action;
                    clip->pasteSelection(mv->cursorPosition(), static_cast<Noggit::Ui::Tools::ChunkManipulator::ChunkPasteFlags>(0), action);

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

    QIcon ChunkTool::toolbarIconOverride() const
    {
        return QIcon(QStringLiteral(":/tool-chunk-manipulator"));
    }

    void ChunkTool::setupUi(Ui::Tools::ToolPanel* toolPanel)
    {
        _chunkManipulator = new Noggit::Ui::Tools::ChunkManipulator::ChunkManipulatorPanel(mapView(), this, mapView());
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

    void ChunkTool::setSelectRadius(float r)
    {
        _select_radius = std::max(CHUNKSIZE * 0.25f, r);
        mapView()->invalidate();
    }

    void ChunkTool::copySelectionToClipboard()
    {
        auto* mv = mapView();
        if (auto* clip = mv->getWorld()->chunkClipboard())
        {
            clip->copySelected(mv->cursorPosition());
        }
        mv->invalidate();
        _painted_select_add = false;
        _painted_select_remove = false;
    }

    void ChunkTool::onSelected()
    {
        _painted_select_add = false;
        _painted_select_remove = false;
        _prev_shift_down = false;
        _prev_ctrl_down = false;
        Tool::onSelected();
    }

    void ChunkTool::onDeselected()
    {
        _painted_select_add = false;
        _painted_select_remove = false;
        _prev_shift_down = false;
        _prev_ctrl_down = false;
        Tool::onDeselected();
    }

    void ChunkTool::onTick(float deltaTime, TickParameters const& params)
    {
        (void)deltaTime;
        auto* mv = mapView();
        if (params.displayMode != display_mode::in_3D || params.underMap)
        {
            _prev_shift_down = params.mod_shift_down;
            _prev_ctrl_down = params.mod_ctrl_down;
            return;
        }

        auto* clip = mv->getWorld()->chunkClipboard();
        if (!clip)
        {
            _prev_shift_down = params.mod_shift_down;
            _prev_ctrl_down = params.mod_ctrl_down;
            return;
        }

        // Released Shift while still holding LMB after painting add-selection: copy now.
        if (_painted_select_add && _prev_shift_down && !params.mod_shift_down && params.left_mouse)
        {
            copySelectionToClipboard();
        }
        // Released Ctrl while still holding LMB after painting deselect: copy now.
        if (_painted_select_remove && _prev_ctrl_down && !params.mod_ctrl_down && params.left_mouse)
        {
            copySelectionToClipboard();
        }

        if (!params.left_mouse)
        {
            _prev_shift_down = params.mod_shift_down;
            _prev_ctrl_down = params.mod_ctrl_down;
            return;
        }

        if (params.mod_shift_down && !params.mod_ctrl_down && !params.mod_alt_down)
        {
            if (auto* action = NOGGIT_ACTION_MGR->beginAction(mv, ActionFlags::eNO_FLAG | ActionFlags::eDO_NOT_WRITE_HISTORY,
                ActionModalityControllers::eSHIFT | ActionModalityControllers::eLMB))
            {
                clip->selectRange(mv->cursorPosition(), _select_radius, Noggit::Ui::Tools::ChunkManipulator::ChunkSelectionMode::SELECT);
                action->setBlockCursor(true);
                _painted_select_add = true;
            }
            mv->invalidate();
        }
        else if (params.mod_ctrl_down && !params.mod_shift_down && !params.mod_alt_down)
        {
            if (auto* action = NOGGIT_ACTION_MGR->beginAction(mv, ActionFlags::eNO_FLAG | ActionFlags::eDO_NOT_WRITE_HISTORY,
                ActionModalityControllers::eCTRL | ActionModalityControllers::eLMB))
            {
                clip->selectRange(mv->cursorPosition(), _select_radius, Noggit::Ui::Tools::ChunkManipulator::ChunkSelectionMode::DESELECT);
                action->setBlockCursor(true);
                _painted_select_remove = true;
            }
            mv->invalidate();
        }

        _prev_shift_down = params.mod_shift_down;
        _prev_ctrl_down = params.mod_ctrl_down;
    }

    void ChunkTool::onMouseRelease(MouseReleaseParameters const& params)
    {
        if (params.button == Qt::MouseButton::LeftButton && (_painted_select_add || _painted_select_remove))
        {
            copySelectionToClipboard();
        }
        Tool::onMouseRelease(params);
    }

    void ChunkTool::onMouseMove(MouseMoveParameters const& params)
    {
        if (!params.mod_alt_down || params.mod_shift_down || params.mod_ctrl_down)
            return;
        if (!params.left_mouse && !params.right_mouse)
            return;

        float const delta = static_cast<float>(params.relative_movement.dx()) / 3.f;
        _select_radius = std::max(CHUNKSIZE * 0.25f, _select_radius + delta);
        mapView()->invalidate();
    }
}
