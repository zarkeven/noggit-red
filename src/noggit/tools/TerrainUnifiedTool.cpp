// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include "TerrainUnifiedTool.hpp"

#include <noggit/ActionManager.hpp>
#include <noggit/Input.hpp>
#include <noggit/MapView.h>
#include <noggit/ui/FlattenTool.hpp>
#include <noggit/ui/TerrainTool.hpp>
#include <noggit/ui/TerrainUnifiedToolWidget.hpp>
#include <noggit/ui/tools/ToolPanel/ToolPanel.hpp>
#include <noggit/ui/tools/ViewToolbar/Ui/ViewToolbar.hpp>
#include <noggit/ui/tools/UiCommon/ImageMaskSelector.hpp>
#include <noggit/World.h>

#include <QtGui/QWheelEvent>

namespace Noggit
{
  TerrainUnifiedTool::TerrainUnifiedTool(MapView* mapView)
    : Tool{ mapView }
  {
    auto const surface_flatten_active = [this, mapView]()
    {
      return mapView->get_editing_mode() == editing_mode::terrain_unified
          && !NOGGIT_CUR_ACTION
          && _widget
          && _widget->activeFamily() == Ui::TerrainUnifiedToolWidget::Family::Surface
          && _widget->surfaceTool();
    };

    addHotkey("nextType"_hash, Hotkey{
      .onPress = [this] { _widget->surfaceTool()->nextFlattenType(); },
      .condition = surface_flatten_active,
    });

    addHotkey("toggleAngle"_hash, Hotkey{
      .onPress = [this] { _widget->surfaceTool()->toggleFlattenAngle(); },
      .condition = surface_flatten_active,
    });

    addHotkey("nextMode"_hash, Hotkey{
      .onPress = [this, mv = mapView]()
      {
        mv->getLeftSecondaryViewToolbar()->nextFlattenMode();
        _widget->surfaceTool()->nextFlattenMode();
      },
      .condition = surface_flatten_active,
    });

    addHotkey("toggleLock"_hash, Hotkey{
      .onPress = [this] { _widget->surfaceTool()->toggleFlattenLock(); },
      .condition = surface_flatten_active,
    });

    addHotkey("lockCursor"_hash, Hotkey{
      .onPress = [this, mv = mapView]() { _widget->surfaceTool()->lockPos(mv->cursorPosition()); },
      .condition = surface_flatten_active,
    });

    addHotkey("increaseRadius"_hash, Hotkey{
      .onPress = [this] { _widget->surfaceTool()->changeRadius(0.01f); },
      .condition = surface_flatten_active,
    });

    addHotkey("decreaseRadius"_hash, Hotkey{
      .onPress = [this] { _widget->surfaceTool()->changeRadius(-0.01f); },
      .condition = surface_flatten_active,
    });
  }

  TerrainUnifiedTool::~TerrainUnifiedTool()
  {
    delete _widget;
  }

  char const* TerrainUnifiedTool::name() const
  {
    return "Terrain";
  }

  editing_mode TerrainUnifiedTool::editingMode() const
  {
    return editing_mode::terrain_unified;
  }

  Ui::FontNoggit::Icons TerrainUnifiedTool::icon() const
  {
    return Ui::FontNoggit::TOOL_RAISE_LOWER;
  }

  void TerrainUnifiedTool::setupUi(Ui::Tools::ToolPanel* toolPanel)
  {
    _widget = new Ui::TerrainUnifiedToolWidget(mapView(), mapView());
    toolPanel->registerTool(this, _widget);

    QObject::connect(_widget, &Ui::TerrainUnifiedToolWidget::activeFamilyChanged, mapView(),
      [mv = mapView()](int family) {
        mv->setTerrainUnifiedSurfaceOverlayVisible(family == static_cast<int>(Ui::TerrainUnifiedToolWidget::Family::Surface));
      });
  }

  void TerrainUnifiedTool::postUiSetup()
  {
    if (!_widget || !_widget->surfaceTool())
      return;

    QObject::connect(mapView()->getLeftSecondaryViewToolbar()
      , &Ui::Tools::ViewToolbar::Ui::ViewToolbar::updateStateRaise
      , [this](bool newState) { _widget->surfaceTool()->_flatten_mode.raise = newState; }
    );

    QObject::connect(mapView()->getLeftSecondaryViewToolbar()
      , &Ui::Tools::ViewToolbar::Ui::ViewToolbar::updateStateLower
      , [this](bool newState) { _widget->surfaceTool()->_flatten_mode.lower = newState; }
    );
  }

  ToolDrawParameters TerrainUnifiedTool::drawParameters() const
  {
    if (!_widget)
      return {};

    if (_widget->activeFamily() == Ui::TerrainUnifiedToolWidget::Family::Surface && _widget->surfaceTool())
    {
      auto* flat = _widget->surfaceTool();
      return {
        .radius = flat->brushRadius(),
        .angle = flat->angle(),
        .orientation = flat->orientation(),
        .ref_pos = flat->ref_pos(),
        .angled_mode = flat->angled_mode(),
        .use_ref_pos = flat->use_ref_pos(),
      };
    }

    if (_widget->sculptTool())
    {
      auto* terrain = _widget->sculptTool();
      CursorType cursorType = CursorType::CIRCLE;
      if ((terrain->_edit_type != eTerrainType_Vertex && terrain->_edit_type != eTerrainType_Script)
          && terrain->getImageMaskSelector()->isEnabled())
      {
        cursorType = CursorType::STAMP;
      }

      return {
        .radius = terrain->brushRadius(),
        .inner_radius = terrain->innerRadius(),
        .cursor_type = cursorType,
        .terrain_type = terrain->_edit_type,
      };
    }

    return {};
  }

  void TerrainUnifiedTool::onSelected()
  {
    if (!_widget)
      return;

    mapView()->setTerrainUnifiedSurfaceOverlayVisible(
      _widget->activeFamily() == Ui::TerrainUnifiedToolWidget::Family::Surface);

    if (_widget->activeFamily() == Ui::TerrainUnifiedToolWidget::Family::Sculpt && _widget->sculptTool())
    {
      auto* terrain = _widget->sculptTool();
      if (terrain->_edit_type != eTerrainType_Vertex
          || (terrain->_edit_type != eTerrainType_Script && terrain->getImageMaskSelector()->isEnabled()))
      {
        terrain->updateMaskImage();
      }
    }
  }

  void TerrainUnifiedTool::setUnifiedFamilyToSculpt()
  {
    if (_widget)
      _widget->setActiveFamily(Ui::TerrainUnifiedToolWidget::Family::Sculpt);
  }

  void TerrainUnifiedTool::setUnifiedFamilyToSurface()
  {
    if (_widget)
      _widget->setActiveFamily(Ui::TerrainUnifiedToolWidget::Family::Surface);
  }

  void TerrainUnifiedTool::onTick(float deltaTime, TickParameters const& params)
  {
    if (!_widget || !params.left_mouse)
      return;

    if (params.displayMode != display_mode::in_3D || params.underMap)
      return;

    auto* mv = mapView();

    if (_widget->activeFamily() == Ui::TerrainUnifiedToolWidget::Family::Surface)
    {
      auto* flat = _widget->surfaceTool();
      if (!flat || !mv->getWorld()->has_selection())
        return;

      if (params.mod_shift_down)
      {
        NOGGIT_ACTION_MGR->beginAction(mv, Noggit::ActionFlags::eCHUNKS_TERRAIN,
          Noggit::ActionModalityControllers::eSHIFT | Noggit::ActionModalityControllers::eLMB);
        flat->flatten(mv->getWorld(), mv->cursorPosition(), deltaTime);
      }
      else if (params.mod_ctrl_down)
      {
        NOGGIT_ACTION_MGR->beginAction(mv, Noggit::ActionFlags::eCHUNKS_TERRAIN,
          Noggit::ActionModalityControllers::eCTRL | Noggit::ActionModalityControllers::eLMB);
        flat->blur(mv->getWorld(), mv->cursorPosition(), deltaTime);
      }
      return;
    }

    auto* terrain = _widget->sculptTool();
    if (!terrain)
      return;

    auto mask_selector = terrain->getImageMaskSelector();
    Noggit::BrushFalloffCurve const* const radial_falloff = _widget->activeRadialFalloff();
    if (params.mod_shift_down && (!mask_selector->isEnabled() || mask_selector->getBrushMode()))
    {
      NOGGIT_ACTION_MGR->beginAction(mv, Noggit::ActionFlags::eCHUNKS_TERRAIN,
        Noggit::ActionModalityControllers::eSHIFT | Noggit::ActionModalityControllers::eLMB);
      terrain->changeTerrain(mv->getWorld(), mv->cursorPosition(), 7.5f * deltaTime, radial_falloff);
    }
    else if (params.mod_ctrl_down && (!mask_selector->isEnabled() || mask_selector->getBrushMode()))
    {
      NOGGIT_ACTION_MGR->beginAction(mv, Noggit::ActionFlags::eCHUNKS_TERRAIN,
        Noggit::ActionModalityControllers::eCTRL | Noggit::ActionModalityControllers::eLMB);
      terrain->changeTerrain(mv->getWorld(), mv->cursorPosition(), -7.5f * deltaTime, radial_falloff);
    }
  }

  void TerrainUnifiedTool::onMouseMove(MouseMoveParameters const& params)
  {
    if (!_widget)
      return;

    if (_widget->activeFamily() == Ui::TerrainUnifiedToolWidget::Family::Surface)
    {
      auto* flat = _widget->surfaceTool();
      if (!flat)
        return;

      if (params.left_mouse)
      {
        if (params.mod_alt_down && !params.mod_shift_down && !params.mod_ctrl_down)
          flat->changeRadius(params.relative_movement.dx() / XSENS);
        if (params.mod_space_down)
          flat->changeSpeed(params.relative_movement.dx() / 30.0f);
      }
      return;
    }

    auto* terrain = _widget->sculptTool();
    if (!terrain)
      return;

    if (params.left_mouse)
    {
      if (params.mod_alt_down && !params.mod_shift_down && !params.mod_ctrl_down)
        terrain->changeRadius(params.relative_movement.dx() / XSENS);
      if (params.mod_space_down)
        terrain->changeSpeed(params.relative_movement.dx() / 30.0f);
    }
  }

  void TerrainUnifiedTool::onMouseWheel(MouseWheelParameters const& params)
  {
    if (!_widget || _widget->activeFamily() != Ui::TerrainUnifiedToolWidget::Family::Surface)
      return;

    auto* flat = _widget->surfaceTool();
    if (!flat)
      return;

    auto delta_for_range = [&](float range) {
      return (params.mod_ctrl_down ? 0.01f : 0.1f)
           * range
           * (params.mod_alt_down ? params.event.angleDelta().x() : params.event.angleDelta().y())
           / 320.f;
    };

    if (params.mod_alt_down)
      flat->changeOrientation(delta_for_range(360.f));
    else if (params.mod_shift_down)
      flat->changeAngle(delta_for_range(89.f));
    else if (params.mod_space_down)
      flat->changeHeight(delta_for_range(40.f));
  }
}
