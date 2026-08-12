// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include "PointLightTool.hpp"

#include <noggit/ActionManager.hpp>
#include <noggit/MapView.h>
#include <noggit/ui/tools/PointLightEditor/PointLightEditor.hpp>
#include <noggit/ui/tools/PointLightEditor/PointLightPropertyDialog.hpp>
#include <noggit/ui/tools/ToolPanel/ToolPanel.hpp>

namespace Noggit
{
  PointLightTool::PointLightTool(::MapView* mapView)
    : Tool{ mapView }
  {
    setupHotkeys();
  }

  PointLightTool::~PointLightTool()
  {
    closePropertyDialogs();
    delete _editor;
  }

  char const* PointLightTool::name() const
  {
    return "Light Creation Tool";
  }

  editing_mode PointLightTool::editingMode() const
  {
    return editing_mode::point_light;
  }

  Ui::FontNoggit::Icons PointLightTool::icon() const
  {
    return Ui::FontNoggit::TOOL_LIGHT;
  }

  void PointLightTool::setupUi(Ui::Tools::ToolPanel* toolPanel)
  {
    _editor = new Noggit::Ui::Tools::PointLightEditor(this, mapView(), toolPanel);
    toolPanel->registerTool(this, _editor);
  }

  void PointLightTool::onSelected()
  {
    mapView()->setDrawPointLightsForEditing(true);
    mapView()->enableGizmoBar();
    if (_editor)
      _editor->refreshFromWorld();
  }

  void PointLightTool::onDeselected()
  {
    closePropertyDialogs();
    mapView()->setDrawPointLightsForEditing(false);
    mapView()->disableGizmoBar();
  }

  void PointLightTool::closePropertyDialogs()
  {
    if (_property_dialog)
    {
      _property_dialog->close();
      _property_dialog = nullptr;
    }
  }

  void PointLightTool::registerContextMenuItems(QMenu* menu)
  {
    addMenuTitle(menu, name());

    bool const has_selection = mapView()->getWorld()->selectedPointLightIndex().has_value();
    bool const has_clipboard = _editor && _editor->clipboardHasLight();

    addMenuItem(menu, "Copy Light", QKeySequence::Copy, has_selection, [=]
    {
      if (_editor)
        _editor->copySelectedToClipboard();
    });

    addMenuItem(menu, "Paste Light", QKeySequence::Paste, has_clipboard, [=]
    {
      if (_editor)
        _editor->pasteFromClipboard();
    });

    addMenuItem(menu, "Delete Light", QKeySequence::Delete, has_selection, [=]
    {
      closePropertyDialogs();
      if (_editor)
        _editor->deleteSelectedLight();
    });
  }

  void PointLightTool::setupHotkeys()
  {
    auto* mv = mapView();

    addHotkey("copySelection"_hash, Hotkey{
      .onPress = [=]
      {
        if (_editor)
          _editor->copySelectedToClipboard();
      },
      .condition = [=]
      {
        return mv->get_editing_mode() == editing_mode::point_light
            && !NOGGIT_CUR_ACTION
            && mv->getWorld()->selectedPointLightIndex().has_value();
      },
    });

    addHotkey("paste"_hash, Hotkey{
      .onPress = [=]
      {
        if (_editor)
          _editor->pasteFromClipboard();
      },
      .condition = [=]
      {
        return mv->get_editing_mode() == editing_mode::point_light
            && !NOGGIT_CUR_ACTION
            && _editor
            && _editor->clipboardHasLight();
      },
    });

    addHotkey("deleteSelection"_hash, Hotkey{
      .onPress = [=]
      {
        closePropertyDialogs();
        if (_editor)
          _editor->deleteSelectedLight();
      },
      .condition = [=]
      {
        return mv->get_editing_mode() == editing_mode::point_light
            && !NOGGIT_CUR_ACTION
            && mv->getWorld()->selectedPointLightIndex().has_value();
      },
    });
  }
}
