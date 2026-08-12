// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include "SoundEmitterTool.hpp"

#include <noggit/MapView.h>
#include <noggit/ui/FontAwesome.hpp>
#include <noggit/ui/tools/SoundEmitterEditor/SoundEmitterEditor.hpp>
#include <noggit/ui/tools/ToolPanel/ToolPanel.hpp>

namespace Noggit
{
  SoundEmitterTool::SoundEmitterTool(::MapView* mapView)
    : Tool{ mapView }
  {
  }

  SoundEmitterTool::~SoundEmitterTool()
  {
    delete _editor;
  }

  char const* SoundEmitterTool::name() const
  {
    return "Sound Emitter Editor";
  }

  editing_mode SoundEmitterTool::editingMode() const
  {
    return editing_mode::sound_emitter;
  }

  Ui::FontNoggit::Icons SoundEmitterTool::icon() const
  {
    return Ui::FontNoggit::SOUND;
  }

  QIcon SoundEmitterTool::toolbarIconOverride() const
  {
    return QIcon(QStringLiteral(":/icons/sound_emitter.png"));
  }

  void SoundEmitterTool::setupUi(Ui::Tools::ToolPanel* toolPanel)
  {
    _editor = new Noggit::Ui::Tools::SoundEmitterEditor(mapView(), toolPanel);
    toolPanel->registerTool(this, _editor);
  }

  void SoundEmitterTool::onSelected()
  {
    mapView()->setDrawSoundEmittersForEditing(true);
    if (_editor)
      _editor->refreshFromWorld();
  }

  void SoundEmitterTool::onDeselected()
  {
    mapView()->setDrawSoundEmittersForEditing(false);
  }
}
