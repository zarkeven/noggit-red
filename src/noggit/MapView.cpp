// This file is part of Noggit3, licensed under GNU General Public License (version 3).
#include <noggit/DBC.h>
#include <noggit/MapChunk.h>
#include <noggit/MapView.h>
#include <noggit/MapHeaders.h>
#include <noggit/Misc.h>
#include <noggit/ModelManager.h> // ModelManager
#include <noggit/TextureManager.h> // TextureManager, Texture
#include <noggit/WMOInstance.h> // WMOInstance
#include <noggit/World.h>
#include <noggit/rendering/PointLightFlicker.hpp>
#include <noggit/MapTile.h>
#include <noggit/map_index.hpp>
#include <noggit/TabletManager.hpp>
#include <opengl/texture.hpp>
#include <noggit/Tool.hpp>
#include <noggit/uid_storage.hpp>
#include <noggit/ui/CurrentTexture.h>
#include <noggit/ui/DetailInfos.h> // detailInfos
#include <noggit/ui/FlattenTool.hpp>
#include <noggit/ui/Help.h>
#include <noggit/ui/HelperModels.h>
#include <noggit/ui/ModelImport.h>
#include <noggit/ui/ObjectEditor.h>
#include <noggit/ui/RotationEditor.h>
#include <noggit/ui/TexturePicker.h>
#include <noggit/ui/TexturingGUI.h>
#include <noggit/ui/RampCreationTool.hpp>
#include <noggit/ui/Toolbar.h> // Noggit::Ui::toolbar
#include <noggit/ui/Water.h>
#include <noggit/ui/ZoneIDBrowser.h>
#include <noggit/ui/windows/noggitWindow/NoggitWindow.hpp>
#include <noggit/ui/minimap_widget.hpp>
#include <noggit/ui/ShaderTool.hpp>
#include <noggit/ui/texture_swapper.hpp>
#include <noggit/ui/texturing_tool.hpp>
#include <noggit/ui/GroundEffectsTool.hpp>
#include <noggit/ui/hole_tool.hpp>
#include <noggit/ui/texture_palette_small.hpp>
#include <noggit/ui/MinimapCreator.hpp>
#include <noggit/project/CurrentProject.hpp>
#include <noggit/integrations/DiscordRichPresence.hpp>
#include <noggit/integrations/EpsilonPatchExporter.hpp>
#include <noggit/application/NoggitApplication.hpp>
#include <opengl/scoped.hpp>
#include <noggit/ui/tools/ViewToolbar/Ui/ViewToolbar.hpp>
#include <noggit/ui/tools/AssetBrowser/Ui/AssetBrowser.hpp>
#include <noggit/ui/tools/PresetEditor/Ui/PresetEditor.hpp>
#include <noggit/ui/tools/NodeEditor/Ui/NodeEditor.hpp>
#include <noggit/ui/tools/UiCommon/ImageBrowser.hpp>
#include <noggit/ui/tools/BrushStack/BrushStack.hpp>
#include <noggit/ui/tools/LightEditor/LightEditor.hpp>
#include <noggit/ui/tools/ToolPanel/ToolPanel.hpp>
#include <noggit/ui/tools/ChunkManipulator/ChunkManipulatorPanel.hpp>
#include <external/imguipiemenu/PieMenu.hpp>
#include <external/tracy/Tracy.hpp>
#include <noggit/ui/object_palette.hpp>
#include <external/glm/gtc/type_ptr.hpp>
#include <external/glm/gtc/matrix_transform.hpp>
#include <external/glm/gtx/euler_angles.hpp>
#include <external/glm/gtx/matrix_decompose.hpp>
#include <external/qtimgui/QtImGui.h>
#include <opengl/types.hpp>
#include <qt-color-widgets/color_selector.hpp>

#include <QAbstractButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QSignalBlocker>
#include <QtCore/QRandomGenerator>
#include <filesystem>
#include <limits>
#include <variant>
#include <noggit/Selection.h>
#include <noggit/ui/FontAwesome.hpp>

#include <noggit/Input.hpp>
#include <noggit/ToolDrawParameters.hpp>
#include <noggit/tools/RaiseLowerTool.hpp>
#include <noggit/tools/FlattenBlurTool.hpp>
#include <noggit/tools/TexturingTool.hpp>
#include <noggit/tools/HoleTool.hpp>
#include <noggit/tools/AreaTool.hpp>
#include <noggit/tools/ImpassTool.hpp>
#include <noggit/tools/WaterTool.hpp>
#include <noggit/tools/VertexPainterTool.hpp>
#include <noggit/tools/ObjectTool.hpp>
#include <noggit/tools/MinimapTool.hpp>
#include <noggit/tools/StampTool.hpp>
#include <noggit/tools/LightTool.hpp>
#include <noggit/tools/PointLightTool.hpp>
#include <noggit/tools/ScriptingTool.hpp>
#include <noggit/tools/ChunkTool.hpp>
#include <noggit/tools/AreaTriggerTool.hpp>
#include <noggit/StringHash.hpp>
#include <noggit/application/NoggitApplication.hpp>
#include <noggit/ActionManager.hpp>
#include <noggit/errorHandling.h>
#include <noggit/Log.h>

#ifdef USE_MYSQL_UID_STORAGE
#include <mysql/mysql.h>

#endif
#include <QtCore/QSettings>

#include <noggit/scripting/scripting_tool.hpp>
#include <noggit/scripting/script_settings.hpp>

#include <noggit/ActionManager.hpp>
#include <noggit/Action.hpp>

#include <noggit/ui/FontNoggit.hpp>

#include <ui_MapViewOverlay.h>

#include "revision.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDockWidget>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QOpenGLWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStatusBar>
#include <QWidgetAction>
#include <QSurfaceFormat>
#include <QMessageBox>
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QDateTime>
#include <QCursor>
#include <QEvent>
#include <QFileDialog>
#include <QProgressDialog>
#include <QClipboard>
#include <QOpenGLContext>
#include <QProcess>
#include <QWidgetAction>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <exception>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include <format>


/* Some ugly macros we use */
// TODO: make those methods instead???

#define DESTRUCTIVE_ACTION(ACTION_CODE)                                                                                \
QMessageBox::StandardButton reply;                                                                                     \
reply = QMessageBox::question(this, "Destructive action", "This action cannot be undone. Current change history will be lost. Continue?", \
QMessageBox::Yes|QMessageBox::No);                                                                                     \
if (reply == QMessageBox::Yes)                                                                                         \
{                                                                                                                      \
NOGGIT_ACTION_MGR->purge();                                                                            \
ACTION_CODE                                                                                                            \
}                                                                                                                      \

// add action no shortcut
#define ADD_ACTION_NS(menu, name, on_action)                      \
  {                                                               \
    auto action (menu->addAction (name));                         \
    connect (action, &QAction::triggered, on_action);             \
  }


#define ADD_TOGGLE(menu_, name_, shortcut_, property_)            \
  do                                                              \
  {                                                               \
    QAction* action (new QAction (name_, this));                  \
    action->setShortcut (QKeySequence (shortcut_));               \
    action->setCheckable (true);                                  \
    action->setChecked (property_.get());                         \
    menu_->addAction (action);                                    \
    connect ( action, &QAction::toggled                           \
            , &property_, &Noggit::BoolToggleProperty::set      \
            );                                                    \
    connect ( &property_, &Noggit::BoolToggleProperty::changed  \
            , action, &QAction::setChecked                        \
            );                                                    \
  }                                                               \
  while (false)


#define ADD_TOGGLE_NS(menu_, name_, property_)                    \
  do                                                              \
  {                                                               \
    QAction* action (new QAction (name_, this));                  \
    action->setCheckable (true);                                  \
    action->setChecked (property_.get());                         \
    menu_->addAction (action);                                    \
    connect ( action, &QAction::toggled                           \
            , &property_, &Noggit::BoolToggleProperty::set      \
            );                                                    \
    connect ( &property_, &Noggit::BoolToggleProperty::changed  \
            , action, &QAction::setChecked                        \
            );                                                    \
  }                                                               \
  while (false)


#define ADD_TOGGLE_POST(menu_, name_, shortcut_, property_, post_)\
  do                                                              \
  {                                                               \
    QAction* action (new QAction (name_, this));                  \
    action->setShortcut (QKeySequence (shortcut_));               \
    action->setCheckable (true);                                  \
    action->setChecked (property_.get());                         \
    menu_->addAction (action);                                    \
    connect ( action, &QAction::toggled                           \
            , &property_, &Noggit::BoolToggleProperty::set      \
            );                                                    \
    connect ( &property_, &Noggit::BoolToggleProperty::changed  \
            , action, &QAction::setChecked                        \
            );                                                    \
    connect ( action, &QAction::toggled, post_);                  \
    connect ( &property_, &Noggit::BoolToggleProperty::changed, \
    post_);                                                       \
  }                                                               \
  while (false)



#define ADD_TOGGLE_NS_POST(menu_, name_, property_, code_)        \
  do                                                              \
  {                                                               \
    QAction* action (new QAction (name_, this));                  \
    action->setCheckable (true);                                  \
    action->setChecked (property_.get());                         \
    menu_->addAction (action);                                    \
    connect ( action, &QAction::toggled                           \
            , &property_, &Noggit::bool_toggle_property::set      \
            );                                                    \
    connect ( &property_, &Noggit::bool_toggle_property::changed  \
            , action, &QAction::setChecked                        \
            );                                                    \
      connect ( action, &QAction::toggled                         \
            ,  code_                                              \
            );                                                    \
    connect ( &property_, &Noggit::bool_toggle_property::changed  \
            , code_                                               \
            );                                                    \
  }                                                               \
  while (false)



#define ADD_ACTION(menu, name, shortcut, on_action)               \
  {                                                               \
    auto action (menu->addAction (name));                         \
    action->setShortcut (QKeySequence (shortcut));                \
    auto callback = on_action;                                    \
    connect (action, &QAction::triggered, [this, callback]()      \
    {                                                             \
       if (NOGGIT_CUR_ACTION) \
        return;                                                   \
       callback();                                                \
                                                                  \
    });                                                           \
  }

using Noggit::XSENS;
using Noggit::YSENS;

void MapView::set_editing_mode(editing_mode mode)
{

  {
    QSignalBlocker const asset_browser_blocker(_asset_browser_dock);

    _asset_browser_dock->hide();
    _viewport_overlay_ui->gizmoBar->hide();
    _viewport_overlay_ui->flattenRaiseLowerBar->hide();
  }

  auto previous_mode = _left_sec_toolbar->getCurrentMode();

  _left_sec_toolbar->setCurrentMode(this, mode);

  // hack to hide empty tools
  if (mode == editing_mode::impass)
  {
    _tool_panel_dock->hide();
  }
  else
  {
    _tool_panel_dock->show();
  }

  if (context() && context()->isValid())
  {
    _world->renderer()->getTerrainParamsUniformBlock()->draw_areaid_overlay = false;
    _world->renderer()->getTerrainParamsUniformBlock()->draw_impass_overlay = false;
    _world->renderer()->getTerrainParamsUniformBlock()->draw_paintability_overlay = false;
    _world->renderer()->getTerrainParamsUniformBlock()->draw_selection_overlay = false;
    _world->renderer()->getTerrainParamsUniformBlock()->draw_groundeffectid_overlay = false;
    _world->renderer()->getTerrainParamsUniformBlock()->draw_groundeffect_layerid_overlay = false;
    _world->renderer()->getTerrainParamsUniformBlock()->draw_noeffectdoodad_overlay = false;
    _world->renderer()->getTerrainParamsUniformBlock()->draw_only_normals = false;
    _world->renderer()->getTerrainParamsUniformBlock()->point_normals_up = false;
    _minimap->use_selection(nullptr);
    
    if (terrainMode != mode)
    {
        activeTool()->onDeselected();
        activeTool(mode);
        activeTool()->onSelected();
    }
  }

  _world->reset_selection();
  emit rotationChanged();

  if (!ui_hidden)
  {
    setToolPropertyWidgetVisibility(mode);
  }

  terrainMode = mode;

  if (mode != editing_mode::point_light)
  {
    _world->selectedPointLightIndex(std::nullopt);
    clearPointLightPropertyEditFallback();
  }

  _toolbar->check_tool (mode);
  this->activateWindow();

  _tool_panel_dock->setWindowTitle(activeTool()->name());

  _world->renderer()->markTerrainParamsUniformBlockDirty();
}

editing_mode MapView::get_editing_mode() const
{
  return terrainMode;
}

void MapView::setToolPropertyWidgetVisibility(editing_mode mode)
{
  _tool_panel_dock->setCurrentTool(mode);

  switch (mode)
  {

  case editing_mode::object:
    _asset_browser_dock->setVisible(!ui_hidden && _settings->value("map_view/asset_browser", false).toBool());
    _viewport_overlay_ui->gizmoBar->setVisible(!ui_hidden);
    _viewport_overlay_ui->flattenRaiseLowerBar->setVisible(false);
    break;

  case editing_mode::flatten_blur:
    _viewport_overlay_ui->gizmoBar->setVisible(false);
    _viewport_overlay_ui->flattenRaiseLowerBar->setVisible(!ui_hidden);
    if (!ui_hidden)
    {
      QSignalBlocker const br(_viewport_overlay_ui->flattenRaiseToggle);
      QSignalBlocker const bl(_viewport_overlay_ui->flattenLowerToggle);
      _viewport_overlay_ui->flattenRaiseToggle->setChecked(_left_sec_toolbar->flattenRaiseChecked());
      _viewport_overlay_ui->flattenLowerToggle->setChecked(_left_sec_toolbar->flattenLowerChecked());
    }
    break;

  default:
    _viewport_overlay_ui->gizmoBar->setVisible(false);
    _viewport_overlay_ui->flattenRaiseLowerBar->setVisible(false);
    break;
  }

  
}

void MapView::ResetSelectedObjectRotation()
{
  if (terrainMode != editing_mode::object)
  {
    return;
  }

  for (auto& selection : _world->current_selection())
  {
    if (selection.index() != eEntry_Object)
      continue;

    auto obj = std::get<selected_object_type>(selection);

    if (obj->which() == eWMO)
    {
      WMOInstance* wmo = static_cast<WMOInstance*>(obj);
      _world->updateTilesWMO(wmo, model_update::remove);
      wmo->resetDirection();
      wmo->recalcExtents();
      _world->updateTilesWMO(wmo, model_update::add);
    }
    else if (obj->which() == eMODEL)
    {
      ModelInstance* m2 = static_cast<ModelInstance*>(obj);
      _world->updateTilesModel(m2, model_update::remove);
      m2->resetDirection();
      m2->recalcExtents();
      _world->updateTilesModel(m2, model_update::add);
    }
  }

  emit rotationChanged();
}

void MapView::snap_selected_models_to_the_ground()
{
  if (terrainMode != editing_mode::object)
  {
    return;
  }

  _world->snap_selected_models_to_the_ground();
  emit rotationChanged();
}

bool MapView::isRotatingCamera() const
{
    return look;
}


void MapView::DeleteSelectedObjects()
{
  if (terrainMode != editing_mode::object)
  {
    return;
  }

  makeCurrent();
  OpenGL::context::scoped_setter const _ (::gl, context());

  _world->delete_selected_models();
  emit rotationChanged();
}

QWidgetAction* MapView::createTextSeparator(const QString& text)
{
  auto* pLabel = new QLabel(text);
  //pLabel->setMinimumWidth(this->minimumWidth() - 4);
  pLabel->setAlignment(Qt::AlignCenter);
  auto* separator = new QWidgetAction(this);
  separator->setDefaultWidget(pLabel);
  return separator;
}

void MapView::enterEvent(QEvent* event)
{
  // check if noggit is the currently active windows
  if (static_cast<QApplication*>(QApplication::instance())->applicationState() & Qt::ApplicationActive)
  {
    activateWindow();
  }
}

void MapView::setupViewportOverlay()
{
  _overlay_widget = new QWidget(this);
  _viewport_overlay_ui = new ::Ui::MapViewOverlay();
  _viewport_overlay_ui->setupUi(_overlay_widget);
  _overlay_widget->setAttribute(Qt::WA_TranslucentBackground);
  _overlay_widget->setMouseTracking(true);
  _overlay_widget->setGeometry(0,0, width(), height());

  _viewport_overlay_ui->gizmoVisibleButton->setIcon(Noggit::Ui::FontNoggitIcon(Noggit::Ui::FontNoggit::Icons::GIZMO_VISIBILITY));
  _viewport_overlay_ui->gizmoModeButton->setIcon(Noggit::Ui::FontNoggitIcon(Noggit::Ui::FontNoggit::Icons::GIZMO_LOCAL));
  _viewport_overlay_ui->gizmoRotateButton->setIcon(Noggit::Ui::FontNoggitIcon(Noggit::Ui::FontNoggit::Icons::GIZMO_ROTATE));
  _viewport_overlay_ui->gizmoScaleButton->setIcon(Noggit::Ui::FontNoggitIcon(Noggit::Ui::FontNoggit::Icons::GIZMO_SCALE));
  _viewport_overlay_ui->gizmoTranslateButton->setIcon(Noggit::Ui::FontNoggitIcon(Noggit::Ui::FontNoggit::Icons::GIZMO_TRANSLATE));

  _viewport_overlay_ui->flattenBarIconLabel->setPixmap(
    Noggit::Ui::FontNoggitIcon(Noggit::Ui::FontNoggit::Icons::TOOL_FLATTEN_BLUR).pixmap(QSize(20, 20)));
  _viewport_overlay_ui->flattenRaiseLowerBar->setFixedHeight(30);
  _viewport_overlay_ui->flattenRaiseLowerBar->setStyleSheet(QStringLiteral(
    "QWidget#flattenRaiseLowerBar {"
    "  background-color: #2a2a2d;"
    "  border: 1px solid #141416;"
    "  border-radius: 2px;"
    "}"
    "QLabel#flattenBarIconLabel {"
    "  background: transparent;"
    "  padding: 0px;"
    "}"
    "QFrame#flattenBarVLine1, QFrame#flattenBarVLine2 {"
    "  background-color: #1c1c1f;"
    "  border: none;"
    "  max-width: 1px;"
    "  min-width: 1px;"
    "}"
    "QCheckBox#flattenRaiseToggle, QCheckBox#flattenLowerToggle {"
    "  color: #e6e6e6;"
    "  spacing: 5px;"
    "  background: transparent;"
    "  padding: 0px 8px;"
    "  font-size: 12px;"
    "}"
    "QCheckBox#flattenRaiseToggle::indicator, QCheckBox#flattenLowerToggle::indicator {"
    "  width: 14px;"
    "  height: 14px;"
    "  border: 1px solid #5a5a62;"
    "  border-radius: 2px;"
    "  background: #3c3c42;"
    "}"
    "QCheckBox#flattenRaiseToggle::indicator:checked, QCheckBox#flattenLowerToggle::indicator:checked {"
    "  background: #4a4a52;"
    "  border: 1px solid #707078;"
    "}"
  ));

  connect(this, &MapView::resized
    ,[this]()
          {
            _overlay_widget->setGeometry(0, 0, width(), height());
          }
  );

  connect(_viewport_overlay_ui->gizmoVisibleButton, &QPushButton::clicked
    ,[this]()
          {
            _gizmo_on.set(_viewport_overlay_ui->gizmoVisibleButton->isChecked());
          }
  );

  connect(&_gizmo_on, &Noggit::BoolToggleProperty::changed
    ,[this](bool state)
          {
            _viewport_overlay_ui->gizmoVisibleButton->setChecked(state);
          }
  );

  connect(_viewport_overlay_ui->gizmoModeButton, &QPushButton::clicked, [this]()
  {
      if (_viewport_overlay_ui->gizmoModeButton->isChecked())
      {
          _gizmo_mode = ImGuizmo::MODE::WORLD;
      }
      else
      {
          _gizmo_mode = ImGuizmo::MODE::LOCAL;
      }
  });

  connect(_viewport_overlay_ui->gizmoTranslateButton, &QPushButton::clicked, [this]() {
      updateGizmoOverlay(ImGuizmo::OPERATION::TRANSLATE);
    });

  connect(_viewport_overlay_ui->gizmoRotateButton, &QPushButton::clicked, [this]() {
      updateGizmoOverlay(ImGuizmo::OPERATION::ROTATE);
    });

  connect(_viewport_overlay_ui->gizmoScaleButton, &QPushButton::clicked, [this]() {
      updateGizmoOverlay(ImGuizmo::OPERATION::SCALE);
    });

  // Keep overlay control in sync with the property default (gizmo on by default).
  _viewport_overlay_ui->gizmoVisibleButton->setChecked(_gizmo_on.get());
}

void MapView::updateGizmoOverlay(ImGuizmo::OPERATION operation)
{
  if (operation == ImGuizmo::OPERATION::TRANSLATE)
  {
    _viewport_overlay_ui->gizmoRotateButton->setChecked(false);
    _viewport_overlay_ui->gizmoScaleButton->setChecked(false);

    if (!_viewport_overlay_ui->gizmoTranslateButton->isChecked())
      _viewport_overlay_ui->gizmoTranslateButton->setChecked(true);
  }

  if (operation == ImGuizmo::OPERATION::ROTATE)
  {
    _viewport_overlay_ui->gizmoTranslateButton->setChecked(false);
    _viewport_overlay_ui->gizmoScaleButton->setChecked(false);

    if (!_viewport_overlay_ui->gizmoRotateButton->isChecked())
      _viewport_overlay_ui->gizmoRotateButton->setChecked(true);
  }

  if (operation == ImGuizmo::OPERATION::SCALE)
  {
    _viewport_overlay_ui->gizmoTranslateButton->setChecked(false);
    _viewport_overlay_ui->gizmoRotateButton->setChecked(false);

    if (!_viewport_overlay_ui->gizmoScaleButton->isChecked())
      _viewport_overlay_ui->gizmoScaleButton->setChecked(true);
  }

  _gizmo_operation = operation;
}

void MapView::setupNodeEditor()
{
  auto _node_editor = new Noggit::Ui::Tools::NodeEditor::Ui::NodeEditorWidget(this);
  _node_editor_dock = new QDockWidget("Node editor", this);
  _node_editor_dock->setWidget(_node_editor);
  _node_editor_dock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea | Qt::LeftDockWidgetArea);

  _main_window->addDockWidget(Qt::LeftDockWidgetArea, _node_editor_dock);
  _node_editor_dock->setFeatures(QDockWidget::DockWidgetMovable
                                 | QDockWidget::DockWidgetFloatable
                                 | QDockWidget::DockWidgetClosable);

  _node_editor_dock->setVisible(_settings->value ("map_view/node_editor", false).toBool());

  connect(_node_editor_dock, &QDockWidget::visibilityChanged,
          [=](bool visible)
          {
            if (ui_hidden)
              return;

            _settings->setValue ("map_view/node_editor", visible);
            _settings->sync();
          });

  connect(this, &QObject::destroyed, _node_editor_dock, &QObject::deleteLater);

  connect ( &_show_node_editor, &Noggit::BoolToggleProperty::changed
    , _node_editor_dock, [this]
            {
              if (!ui_hidden)
                _node_editor_dock->setVisible(_show_node_editor.get());
            }
  );

  connect ( _node_editor_dock, &QDockWidget::visibilityChanged
    , &_show_node_editor, &Noggit::BoolToggleProperty::set
  );

}

void MapView::setupAssetBrowser()
{
  _asset_browser_dock = new QDockWidget("Asset browser", this);
  _asset_browser = new Noggit::Ui::Tools::AssetBrowser::Ui::AssetBrowserWidget(this, this);

  //_main_window->addDockWidget(Qt::BottomDockWidgetArea, _asset_browser_dock);
  _asset_browser_dock->setFeatures(QDockWidget::DockWidgetMovable
                                   | QDockWidget::DockWidgetFloatable
                                   | QDockWidget::DockWidgetClosable);
  _asset_browser_dock->setAllowedAreas(Qt::NoDockWidgetArea);

  _asset_browser_dock->setFloating(true);
  _asset_browser_dock->hide();

  _asset_browser_dock->setWidget(_asset_browser);
  _asset_browser_dock->setWindowFlags(
    Qt::CustomizeWindowHint |
    Qt::Window | 
    Qt::WindowMinimizeButtonHint |
    Qt::WindowMaximizeButtonHint |
    Qt::WindowCloseButtonHint | 
    Qt::WindowStaysOnTopHint);

  connect(_asset_browser_dock, &QDockWidget::visibilityChanged,
          [=](bool visible)
          {
            if (ui_hidden)
              return;

            _settings->setValue ("map_view/asset_browser", visible);
            _settings->sync();
          });;

  connect(this, &QObject::destroyed, _asset_browser_dock, &QObject::deleteLater);
}

void MapView::setupDetailInfos()
{

  // Dock
  _detail_infos_dock = new QDockWidget("Detail info", this);
  _detail_infos_dock->setFeatures(QDockWidget::DockWidgetMovable
                                  | QDockWidget::DockWidgetFloatable
                                  | QDockWidget::DockWidgetClosable);

  _detail_infos_dock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea | Qt::LeftDockWidgetArea);


  _main_window->addDockWidget(Qt::BottomDockWidgetArea, _detail_infos_dock);
  _detail_infos_dock->setFloating(true);
  _detail_infos_dock->hide();
  // End Dock

  guidetailInfos = new Noggit::Ui::detail_infos(this);
  _detail_infos_dock->setWidget(guidetailInfos);


  connect ( &_show_detail_info_window, &Noggit::BoolToggleProperty::changed
    , guidetailInfos, [this]
            {
              if (!ui_hidden)
              {
                  _detail_infos_dock->setVisible(_show_detail_info_window.get());
                  updateDetailInfos();
              }
            }
  );

  connect ( guidetailInfos, &Noggit::Ui::widget::visibilityChanged
    , &_show_detail_info_window, &Noggit::BoolToggleProperty::set
  );

  connect(NOGGIT_ACTION_MGR, &Noggit::ActionManager::onActionBegin,
    [this](Noggit::Action*)
    {
      updateDetailInfos();
    });

  connect(NOGGIT_ACTION_MGR, &Noggit::ActionManager::onActionEnd,
    [this](Noggit::Action*)
    {
      updateDetailInfos();
    });

  connect(NOGGIT_ACTION_MGR, &Noggit::ActionManager::currentActionChanged,
    [this](unsigned)
    {
      updateDetailInfos();
    });
}

void MapView::updateDetailInfos()
{
  auto& current_selection = _world->current_selection();

  // update detail infos TODO: selection update signal.


  if (guidetailInfos->isVisible())
  {
    if (!current_selection.empty())
    {
      selection_type& selection_last = const_cast<selection_type&>(current_selection.back());

      switch (selection_last.index())
      {
        case eEntry_Object:
        {
          auto obj = std::get<selected_object_type>(selection_last);
          obj->updateDetails(guidetailInfos);
          break;
        }
        case eEntry_MapChunk:
        {
          selected_chunk_type& chunk_sel(std::get<selected_chunk_type>(selection_last));
          chunk_sel.updateDetails(guidetailInfos);
          break;
        }
      }
    }
    else
    {
      guidetailInfos->setText("");
    }
  }
}

void MapView::setupToolbars()
{
  _toolbar = new Noggit::Ui::toolbar(_tools, [this] (editing_mode mode) { set_editing_mode (mode); });
  _toolbar->setOrientation(Qt::Vertical);
  auto left_toolbar_layout = new QVBoxLayout(_viewport_overlay_ui->leftToolbarHolder);
  left_toolbar_layout->addWidget( _toolbar);
  left_toolbar_layout->setDirection(QBoxLayout::LeftToRight);
  left_toolbar_layout->setContentsMargins(0, 5, 0, 5);
  connect (this, &QObject::destroyed, _toolbar, &QObject::deleteLater);

  auto left_sec_toolbar_layout = new QVBoxLayout(_viewport_overlay_ui->leftSecondaryToolbarHolder);
  left_sec_toolbar_layout->setContentsMargins(5, 0, 5, 0);

  _left_sec_toolbar = new Noggit::Ui::Tools::ViewToolbar::Ui::ViewToolbar(this, terrainMode);
  connect(this, &QObject::destroyed, _left_sec_toolbar, &QObject::deleteLater);
  left_sec_toolbar_layout->addWidget( _left_sec_toolbar);
  // Hide the extra left secondary toolbar strip by default.
  _left_sec_toolbar->hide();

  auto top_toolbar_layout = new QVBoxLayout(_viewport_overlay_ui->upperToolbarHolder);
  top_toolbar_layout->setContentsMargins(5, 0, 5, 0);
  auto sec_toolbar_layout = new QVBoxLayout(_viewport_overlay_ui->secondaryToolbarHolder);
  sec_toolbar_layout->setContentsMargins(5, 0, 5, 0);

  _viewport_overlay_ui->secondaryToolbarHolder->hide();
  _secondary_toolbar = new Noggit::Ui::Tools::ViewToolbar::Ui::ViewToolbar(this);
  connect (this, &QObject::destroyed, _secondary_toolbar, &QObject::deleteLater);

  _view_toolbar = new Noggit::Ui::Tools::ViewToolbar::Ui::ViewToolbar(this, _secondary_toolbar);
  connect (this, &QObject::destroyed, _view_toolbar, &QObject::deleteLater);

  top_toolbar_layout->addWidget( _view_toolbar);
  sec_toolbar_layout->addWidget( _secondary_toolbar);

  // Flatten / Blur: Raise + Lower on the viewport (left of the object gizmo bar). Keeps controls visible for classic UI too.
  connect(_viewport_overlay_ui->flattenRaiseToggle, &QAbstractButton::toggled, this,
          [this](bool on) { _left_sec_toolbar->setFlattenRaiseChecked(on); });
  connect(_viewport_overlay_ui->flattenLowerToggle, &QAbstractButton::toggled, this,
          [this](bool on) { _left_sec_toolbar->setFlattenLowerChecked(on); });
  connect(_left_sec_toolbar, &Noggit::Ui::Tools::ViewToolbar::Ui::ViewToolbar::updateStateRaise, this,
          [this](bool on)
          {
            QSignalBlocker const b(_viewport_overlay_ui->flattenRaiseToggle);
            _viewport_overlay_ui->flattenRaiseToggle->setChecked(on);
          });
  connect(_left_sec_toolbar, &Noggit::Ui::Tools::ViewToolbar::Ui::ViewToolbar::updateStateLower, this,
          [this](bool on)
          {
            QSignalBlocker const b(_viewport_overlay_ui->flattenLowerToggle);
            _viewport_overlay_ui->flattenLowerToggle->setChecked(on);
          });
}

void MapView::setupMainToolbar()
{
    _main_window->_app_toolbar = new QToolBar("Menu Toolbar", this); // this or mainwindow as parent?
    connect(this, &QObject::destroyed, _main_window->_app_toolbar, &QObject::deleteLater);

    _main_window->_app_toolbar->setOrientation(Qt::Horizontal);
    _main_window->addToolBar(_main_window->_app_toolbar);
    _main_window->_app_toolbar->setVisible(_settings->value("map_view/app_toolbar", false).toBool()); // hide by default.

    connect(_main_window->_app_toolbar, &QToolBar::visibilityChanged,
        [=](bool visible)
        {
            if (ui_hidden)
                return;

            _settings->setValue("map_view/app_toolbar", visible);
            _settings->sync();
        });

    // TODO
    /*
    auto save_changed_btn = new QPushButton(this);
    save_changed_btn->setIcon(Noggit::Ui::FontAwesomeIcon(Noggit::Ui::FontAwesome::save));
    save_changed_btn->setToolTip("Save Changed");
    // save_changed_btn->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_S));
    _main_window->_app_toolbar->addWidget(save_changed_btn);

    auto undo_btn = new QPushButton(this);
    undo_btn->setIcon(Noggit::Ui::FontAwesomeIcon(Noggit::Ui::FontAwesome::undo));
    undo_btn->setToolTip("Undo");
    // undo_btn->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z));
    _main_window->_app_toolbar->addWidget(undo_btn);

    auto redo_btn = new QPushButton(this);
    redo_btn->setIcon(Noggit::Ui::FontAwesomeIcon(Noggit::Ui::FontAwesome::redo));
    redo_btn->setToolTip("Undo");
    // redo_btn->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));
    _main_window->_app_toolbar->addWidget(redo_btn);

    _main_window->_app_toolbar->addSeparator();

    QAction* start_server_action = _main_window->_app_toolbar->addAction("Start Server");
    start_server_action->setToolTip("Start World and Auth servers.");
    start_server_action->setIcon(Noggit::Ui::FontAwesomeIcon(Noggit::Ui::FontAwesome::server));
    
    QAction* extract_server_map_action = _main_window->_app_toolbar->addAction("Extract Server Map");
    extract_server_map_action->setToolTip("Start server extractors for this map.");
    // TODO idea : detect modified tiles and only extract those.
    extract_server_map_action->setIcon(Noggit::Ui::FontAwesomeIcon(Noggit::Ui::FontAwesome::map));
*/

    auto build_data_btn = new QPushButton(this); 
    _main_window->_app_toolbar->addWidget(build_data_btn);
    build_data_btn->setToolTip("Save content of project folder as MPQ patch in the client.");
    build_data_btn->setIcon(Noggit::Ui::FontAwesomeIcon(Noggit::Ui::FontAwesome::filearchive));
    connect(build_data_btn, &QPushButton::clicked
        , [=]()
        {
            _main_window->patchWowClient(); // code to open dialog

        });

    auto start_wow_btn = new QPushButton(this);
    start_wow_btn->setIcon(Noggit::Ui::FontAwesomeIcon(Noggit::Ui::FontAwesome::play));
    start_wow_btn->setToolTip("Launch the client");
    _main_window->_app_toolbar->addWidget(start_wow_btn);

    connect(start_wow_btn, &QPushButton::clicked
        , [=]()
        {
            _main_window->startWowClient();
        });


    // TODO : restart button while WoW is running?

  // IDEAs : various client utils like synchronize client view with noggit, reload, patch WoW.exe with community patches like unlock md5 check, set WoW client version
}

std::unique_ptr<Noggit::Tool>& MapView::activeTool()
{
    return _tools[_activeToolIndex];
}

void MapView::activeTool(editing_mode newTool)
{
    for (size_t i = 0; i < _tools.size(); ++i)
    {
        if (_tools[i]->editingMode() == newTool)
        {
            _activeToolIndex = i;
            return;
        }
    }

    throw std::exception{ std::format("Tried to call MapView::activeTool with invalid editing_mode `{}`!", static_cast<int>(newTool)).c_str() };
}

Noggit::Ui::Tools::ViewToolbar::Ui::ViewToolbar* MapView::getLeftSecondaryViewToolbar()
{
    return _left_sec_toolbar;
}

QSettings* MapView::settings()
{
    return _settings;
}

Noggit::Ui::Windows::NoggitWindow* MapView::mainWindow()
{
    return _main_window;
}

bool MapView::isUiHidden() const
{
    return ui_hidden;
}

bool MapView::drawAdtGrid() const
{
    return _draw_lines.get();
}

bool MapView::drawHoleGrid() const
{
    return _draw_hole_lines.get();
}

void MapView::invalidate()
{
    _needs_redraw = true;
}

std::optional<std::size_t> MapView::resolvePointLightPropertyEditIndex(QListWidget* panel_point_list) const
{
  if (!_world)
    return std::nullopt;
  auto const& lights = _world->pointLights();
  if (panel_point_list)
  {
    int const r = panel_point_list->currentRow();
    if (r >= 0 && r < static_cast<int>(lights.size()))
      return static_cast<std::size_t>(r);
  }
  if (auto const w = _world->selectedPointLightIndex();
      w.has_value() && *w < lights.size())
    return *w;
  if (_point_light_property_edit_fallback.has_value()
      && *_point_light_property_edit_fallback < lights.size())
    return *_point_light_property_edit_fallback;
  return std::nullopt;
}

void MapView::setPointLightPropertyEditFallback(std::optional<std::size_t> row_index)
{
  if (!row_index.has_value() || !_world || *row_index >= _world->pointLights().size())
  {
    _point_light_property_edit_fallback.reset();
    return;
  }
  _point_light_property_edit_fallback = row_index;
}

void MapView::clearPointLightPropertyEditFallback()
{
  _point_light_property_edit_fallback.reset();
}

bool MapView::pointLightViewportTransformBlocked() const noexcept
{
  return _point_light_gizmo_edit_action || _point_light_numpad_edit_action || _point_light_mmb_edit_action;
}

void MapView::touchPointLightPropertyUndoBatch()
{
  if (!_world)
    return;
  bool const have_edit_target =
      (_world->selectedPointLightIndex().has_value()
       && *_world->selectedPointLightIndex() < _world->pointLights().size())
      || (_point_light_property_edit_fallback.has_value()
          && *_point_light_property_edit_fallback < _world->pointLights().size());
  if (!have_edit_target)
    return;

  if (_point_light_gizmo_edit_action || _point_light_numpad_edit_action)
    return;

  if (NOGGIT_CUR_ACTION)
  {
    if (!_point_light_property_undo_session
        || (NOGGIT_CUR_ACTION->getFlags() & Noggit::ActionFlags::ePOINT_LIGHTS_CHANGED) == 0
        || NOGGIT_CUR_ACTION->getModalityControllers() != Noggit::ActionModalityControllers::eNONE)
      return;

    _point_light_property_undo_timer.start(400);
    return;
  }

  if (Noggit::Action* const action = NOGGIT_ACTION_MGR->beginAction(this,
                                                                    Noggit::ActionFlags::ePOINT_LIGHTS_CHANGED,
                                                                    Noggit::ActionModalityControllers::eNONE))
  {
    action->registerPointLightsChange();
    _point_light_property_undo_session = true;
    _point_light_property_undo_timer.start(400);
  }
}

void MapView::flushPointLightPropertyUndoBatch()
{
  if (_point_light_property_undo_timer.isActive())
    _point_light_property_undo_timer.stop();

  if (!_point_light_property_undo_session)
    return;

  if (NOGGIT_CUR_ACTION
      && (NOGGIT_CUR_ACTION->getFlags() & Noggit::ActionFlags::ePOINT_LIGHTS_CHANGED)
      && NOGGIT_CUR_ACTION->getModalityControllers() == Noggit::ActionModalityControllers::eNONE)
  {
    NOGGIT_ACTION_MGR->endAction();
  }

  _point_light_property_undo_session = false;
}

void MapView::recordPointLightListChange(std::function<void()> mut)
{
  flushPointLightPropertyUndoBatch();

  if (NOGGIT_CUR_ACTION)
  {
    mut();
    return;
  }

  if (Noggit::Action* const action = NOGGIT_ACTION_MGR->beginAction(this,
                                                                    Noggit::ActionFlags::ePOINT_LIGHTS_CHANGED,
                                                                    Noggit::ActionModalityControllers::eNONE))
  {
    action->registerPointLightsChange();
    mut();
    NOGGIT_ACTION_MGR->endAction();
  }
  else
  {
    mut();
  }
}

void MapView::setPointLightGizmoSuppressedForColorPick(bool suppressed)
{
  if (_point_light_suppress_gizmo_for_color_pick == suppressed)
    return;
  _point_light_suppress_gizmo_for_color_pick = suppressed;
  if (suppressed && _point_light_gizmo_edit_action)
  {
    if (NOGGIT_CUR_ACTION)
      NOGGIT_ACTION_MGR->endAction();
    _point_light_gizmo_edit_action = false;
  }
  invalidate();
}

bool MapView::pointLightColorDialogOwnedByRegisteredPicker(QWidget* dialog) const
{
  QWidget* p = dialog->parentWidget();
  while (p)
  {
    for (auto const& reg : _point_light_color_pickers)
      if (reg && p == reg.data())
        return true;
    p = p->parentWidget();
  }
  return false;
}

void MapView::registerPointLightColorPicker(QWidget* picker)
{
  if (!picker)
    return;
  for (auto const& existing : _point_light_color_pickers)
    if (existing && existing.data() == picker)
      return;

  _point_light_color_pickers.emplace_back(picker);
  picker->installEventFilter(this);
  connect(picker, &QObject::destroyed, this, [this](QObject* o) {
    auto* dead = static_cast<QWidget*>(o);
    _point_light_color_pickers.erase(
      std::remove_if(_point_light_color_pickers.begin(), _point_light_color_pickers.end(),
                     [dead](QPointer<QWidget> const& ptr) { return !ptr || ptr.data() == dead; }),
      _point_light_color_pickers.end());
  });
}

bool MapView::eventFilter(QObject* watched, QEvent* event)
{
  // Only hide the point-light gizmo while a modal QColorDialog opened from our pickers is visible.
  // (Do not suppress on every click on the color widget — that hid the gizmo for all panel use.)
  if (event->type() == QEvent::Show)
  {
    auto* const win = qobject_cast<QWidget*>(watched);
    if (win && win->isWindow()
        && std::strcmp(win->metaObject()->className(), "QColorDialog") == 0
        && pointLightColorDialogOwnedByRegisteredPicker(win))
    {
      _point_light_active_color_dialog = win;
      setPointLightGizmoSuppressedForColorPick(true);
    }
  }
  else if (event->type() == QEvent::Hide)
  {
    auto* const win = qobject_cast<QWidget*>(watched);
    if (win && win->isWindow()
        && std::strcmp(win->metaObject()->className(), "QColorDialog") == 0
        && _point_light_suppress_gizmo_for_color_pick)
    {
      if (_point_light_active_color_dialog && watched != _point_light_active_color_dialog)
        return false;
      _point_light_active_color_dialog.clear();
      setPointLightGizmoSuppressedForColorPick(false);
    }
  }
  return false;
}

void MapView::selectObjects(std::array<glm::vec2, 2> selection_box, float depth)
{
    _world->select_objects_in_area(selection_box, !_mod_shift_down, _model_view, _projection, width(), height(), depth, _camera.position);
}

std::shared_ptr<Noggit::Project::NoggitProject>& MapView::project()
{
    return _project;
}

float MapView::timeSpeed() const
{
    return mTimespeed;
}

void MapView::setupKeybindingsGui()
{
  _keybindings = new Noggit::Ui::help(this);
  _keybindings->hide();
  connect(this, &QObject::destroyed, _keybindings, &QObject::deleteLater);

  connect ( &_show_keybindings_window, &Noggit::BoolToggleProperty::changed
    , _keybindings, &QWidget::setVisible
  );

  connect ( _keybindings, &Noggit::Ui::widget::visibilityChanged
    , &_show_keybindings_window, &Noggit::BoolToggleProperty::set
  );
}

void MapView::setupFileMenu()
{
  auto file_menu (_main_window->_menuBar->addMenu ("Editor"));
  connect (this, &QObject::destroyed, file_menu, &QObject::deleteLater);

  ADD_ACTION (file_menu, "Save current tile", "Ctrl+Shift+S", [this] { save(save_mode::current); emit saved();});
  ADD_ACTION (file_menu, "Save changed tiles", QKeySequence::Save, [this] { save(save_mode::changed); emit saved(); });
  ADD_ACTION (file_menu, "Save all tiles", "Ctrl+Shift+A", [this] { save(save_mode::all); emit saved(); });
  ADD_ACTION(file_menu, "Generate new WDL", "", [this] 
      { 
     QMessageBox prompt;
    prompt.setIcon(QMessageBox::Warning);
    prompt.setWindowFlags(Qt::WindowStaysOnTopHint);
     prompt.setText(std::string("Warning!\nThis will attempt to load all tiles in the map to generate a new WDL."
         "\nThis is likely to crash if there is any issue with any tile, it is recommended that you save your work first. Only use this if you really need a fresh WDL.").c_str());
     prompt.setInformativeText(std::string("Are you sure ?").c_str());
     prompt.setStandardButtons(QMessageBox::StandardButton::Yes | QMessageBox::StandardButton::No);
     prompt.setDefaultButton(QMessageBox::No);
     bool answer = prompt.exec() == QMessageBox::StandardButton::Yes;
     if (answer)
         _world->horizon.save_wdl(_world.get(), true);
      }
  );

  ADD_ACTION ( file_menu
  , "Reload tile"
  , "Shift+J"
  , [this]
               {
                 makeCurrent();
                 OpenGL::context::scoped_setter const _ (::gl, context());
                 _world->reload_tile (_camera.position);
                 emit rotationChanged();
                 emit saved();
               }
  );

  file_menu->addSeparator();
  ADD_ACTION_NS (file_menu, "Force uid check on next opening", [this] { _force_uid_check = true; });
  file_menu->addSeparator();

  ADD_ACTION ( file_menu
  , "Add bookmark"
  , Qt::CTRL | Qt::Key_F5
      , [this]
      {

          auto bookmark = Noggit::Project::NoggitProjectBookmarkMap();
          bookmark.position = _camera.position;
          bookmark.camera_pitch = _camera.pitch()._;
          bookmark.camera_yaw = _camera.yaw()._;
          bookmark.map_id = _world->getMapID();
          bookmark.name = gAreaDB.getAreaFullName(_world->getAreaID(_camera.position));

        _project->createBookmark(bookmark);

      }
  );

  ADD_ACTION(file_menu
      , "Write coordinates to port.txt and copy to clipboard"
      , Qt::Key_G
      , [this]
      {
                 std::stringstream port_command;
                 port_command << ".go XYZ " << (ZEROPOINT - _camera.position.z) << " " << (ZEROPOINT - _camera.position.x) << " " << _camera.position.y << " " << _world->getMapID();
                 std::ofstream f("ports.txt", std::ios_base::app);
                 f << "Map: " << gAreaDB.getAreaFullName(_world->getAreaID (_camera.position)) << " on ADT " << std::floor(_camera.position.x / TILESIZE) << " " << std::floor(_camera.position.z / TILESIZE) << std::endl;
                 f << "Trinity/AC:" << std::endl << port_command.str() << std::endl;
                 // f << "ArcEmu:" << std::endl << ".worldport " << _world->getMapID() << " " << (ZEROPOINT - _camera.position.z) << " " << (ZEROPOINT - _camera.position.x) << " " << _camera.position.y << " " << std::endl << std::endl;
                 f.close();
                 QClipboard* clipboard = QGuiApplication::clipboard();
                 clipboard->setText(port_command.str().c_str(), QClipboard::Clipboard);
               }
  );

  ADD_ACTION_NS (file_menu, "Ramp Creation Tool", [this] { ensureRampToolWindow(); });

}

void MapView::setupEditMenu()
{
  auto edit_menu (_main_window->_menuBar->addMenu ("Edit"));
  connect (this, &QObject::destroyed, edit_menu, &QObject::deleteLater);

  edit_menu->addSeparator();
  edit_menu->addAction(createTextSeparator("Selected object"));
  edit_menu->addSeparator();
  ADD_ACTION(edit_menu, "Delete", Qt::Key_Delete, [this]
    {
      if (get_editing_mode() == editing_mode::object)
      {
        NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eOBJECTS_REMOVED);
        DeleteSelectedObjects();
        NOGGIT_ACTION_MGR->endAction();
      }
      else
      {
        for (auto&& hotkey : hotkeys)
        {
          if (Qt::Key_Delete == hotkey.key && hotkey.condition())
          {
            makeCurrent();
            OpenGL::context::scoped_setter const _(::gl, context());

            hotkey.onPress();
            return;
          }
        }
      }
    }
  );

  ADD_ACTION (edit_menu, "Reset rotation", "Ctrl+R",
              [this]
              {
                NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eOBJECTS_TRANSFORMED);
                ResetSelectedObjectRotation();
                NOGGIT_ACTION_MGR->endAction();
              });
  ADD_ACTION (edit_menu, "Set to ground", Qt::Key_PageDown,
              [this] {
                NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eOBJECTS_TRANSFORMED);
                snap_selected_models_to_the_ground();
                NOGGIT_ACTION_MGR->endAction();

              });

  edit_menu->addSeparator();
  edit_menu->addAction(createTextSeparator("Options"));
  edit_menu->addSeparator();
  ADD_TOGGLE_NS (edit_menu, "Locked cursor mode", _locked_cursor_mode);

  edit_menu->addSeparator();
  edit_menu->addAction(createTextSeparator("State"));
  edit_menu->addSeparator();
  ADD_ACTION (edit_menu, "Undo", "Ctrl+Z", [this] { NOGGIT_ACTION_MGR->undo(); });
  ADD_ACTION (edit_menu, "Redo", "Ctrl+Shift+Z", [this] { NOGGIT_ACTION_MGR->redo(); });
}

void MapView::setupAssistMenu()
{
  auto assist_menu (_main_window->_menuBar->addMenu ("Assist"));
  connect (this, &QObject::destroyed, assist_menu, &QObject::deleteLater);

  assist_menu->addSeparator();
  assist_menu->addAction(createTextSeparator("Current ADT"));
  assist_menu->addSeparator();

  ADD_ACTION_NS ( assist_menu
  , "Ensure 4 texture layers"
  , [=]
    {
      makeCurrent();
      OpenGL::context::scoped_setter const _(::gl, context());

      NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNKS_TEXTURE);
      _world->ensureAllTilesetsADT(_camera.position);
      NOGGIT_ACTION_MGR->endAction();

    }
  );

  auto cleanup_menu (assist_menu->addMenu ("Clean up"));

  ADD_ACTION_NS ( cleanup_menu
  , "Clear height map"
  , [this]
                  {
                    makeCurrent();
                    OpenGL::context::scoped_setter const _ (::gl, context());
                    NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNKS_TERRAIN);
                    _world->clearHeight(_camera.position);
                    NOGGIT_ACTION_MGR->endAction();
                  }
  );
  ADD_ACTION_NS ( cleanup_menu
  , "Remove texture duplicates"
  , [this]
                  {
                    makeCurrent();
                    OpenGL::context::scoped_setter const _ (::gl, context());
                    NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNKS_TEXTURE);
                    _world->removeTexDuplicateOnADT(_camera.position);
                    NOGGIT_ACTION_MGR->endAction();
                  }
  );
  ADD_ACTION_NS ( cleanup_menu
  , "Clear textures"
  , [this]
                  {
                    makeCurrent();
                    OpenGL::context::scoped_setter const _ (::gl, context());
                    NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNKS_TEXTURE);
                    _world->clearTextures(_camera.position);
                    NOGGIT_ACTION_MGR->endAction();
                  }
  );
  ADD_ACTION_NS ( cleanup_menu
  , "Clear textures + set base"
  , [this]
                  {
                    makeCurrent();
                    OpenGL::context::scoped_setter const _ (::gl, context());
                    NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNKS_TEXTURE);
                    _world->setBaseTexture(_camera.position);
                    NOGGIT_ACTION_MGR->endAction();
                  }
  );
  ADD_ACTION_NS ( cleanup_menu
  , "Clear shadows"
  , [this]
                  {
                    makeCurrent();
                    OpenGL::context::scoped_setter const _ (::gl, context());
                    NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNK_SHADOWS);
                    _world->clear_shadows(_camera.position);
                    NOGGIT_ACTION_MGR->endAction();
                  }
  );
  ADD_ACTION_NS ( cleanup_menu
  , "Clear models"
  , [this]
                  {
                    makeCurrent();
                    OpenGL::context::scoped_setter const _ (::gl, context());
                    NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eOBJECTS_REMOVED);
                    _world->clearAllModelsOnADT(_camera.position, true);
                    NOGGIT_ACTION_MGR->endAction();
                    emit rotationChanged();
                  }
  );
  ADD_ACTION_NS ( cleanup_menu
  , "Clear duplicate models"
  , [this]
                  {
                    DESTRUCTIVE_ACTION
                      (
                        makeCurrent();
                    OpenGL::context::scoped_setter const _(::gl, context());
                    _world->delete_duplicate_model_and_wmo_instances();
                    )
                  }
  );

  auto cur_adt_export_menu(assist_menu->addMenu("Export"));
  ADD_ACTION_NS ( cur_adt_export_menu
  , "Export alphamaps"
  , [this]
  {
    makeCurrent();
    OpenGL::context::scoped_setter const _(::gl, context());
    _world->exportADTAlphamap(_camera.position);
  }
  );

  ADD_ACTION_NS ( cur_adt_export_menu
  , "Export alphamaps (current texture)"
  , [this]
  {
    makeCurrent();
    OpenGL::context::scoped_setter const _(::gl, context());

    if (!!Noggit::Ui::selected_texture::get())
    {
      _world->exportADTAlphamap(_camera.position, Noggit::Ui::selected_texture::get()->get()->file_key().filepath());
    }

  }
  );

  ADD_ACTION_NS ( cur_adt_export_menu
  , "Export vertex color map"
  , [this]
                  {
                    makeCurrent();
                    OpenGL::context::scoped_setter const _(::gl, context());

                    _world->exportADTVertexColorMap(_camera.position);
                  }
  );

  // vertices can support up to 32bit but other things break at 16bit like WDL and MFBO
  //  DB/ZoneLight appears to be using -64000 and 64000
  //  DB/DungeonMapChunk seems to use -10000 for lower default.
  int constexpr MIN_HEIGHT = std::numeric_limits<short>::min(); // -32768
  int constexpr MAX_HEIGHT = std::numeric_limits<short>::max(); // 32768

  int constexpr DEFAULT_MIN_HEIGHT = -2000; // outland goes to -1200
  int constexpr DEFAULT_MAX_HEIGHT = 3000; // hyjal goes to 2000

  QDialog* heightmap_export_params = new QDialog(this);
  heightmap_export_params->setWindowFlags(Qt::Popup);
  heightmap_export_params->setWindowTitle("Heightmap Exporter");
  QVBoxLayout* heightmap_export_params_layout = new QVBoxLayout(heightmap_export_params);

  heightmap_export_params_layout->addWidget(new QLabel("Import with the same values \nto keep the same coordinates.",
      heightmap_export_params));

  heightmap_export_params_layout->addWidget(new QLabel("Min Height:", heightmap_export_params));
  QDoubleSpinBox* heightmap_export_min = new QDoubleSpinBox(heightmap_export_params);
  heightmap_export_min->setRange(MIN_HEIGHT, MAX_HEIGHT);
  heightmap_export_min->setValue(DEFAULT_MIN_HEIGHT);
  heightmap_export_params_layout->addWidget(heightmap_export_min);

  heightmap_export_params_layout->addWidget(new QLabel("Max Height:", heightmap_export_params));
  QDoubleSpinBox* heightmap_export_max = new QDoubleSpinBox(heightmap_export_params);
  heightmap_export_max->setRange(MIN_HEIGHT, MAX_HEIGHT);
  heightmap_export_max->setValue(DEFAULT_MAX_HEIGHT);
  heightmap_export_params_layout->addWidget(heightmap_export_max);

  std::string const autoheights_tooltip_str = "Sets fields to this tile's min and max heights\nDefaults : Min: "
      + std::to_string(DEFAULT_MIN_HEIGHT) + ", Max: " + std::to_string(DEFAULT_MAX_HEIGHT);
  QPushButton* heightmap_export_params_auto_height = new QPushButton("Auto Heights", heightmap_export_params);
  heightmap_export_params_auto_height->setToolTip(autoheights_tooltip_str.c_str());
  heightmap_export_params_layout->addWidget(heightmap_export_params_auto_height);

  QPushButton* heightmap_export_okay = new QPushButton("Okay", heightmap_export_params);
  heightmap_export_params_layout->addWidget(heightmap_export_okay);

  connect(heightmap_export_min, qOverload<double>(&QDoubleSpinBox::valueChanged),
          [=](double value)
          {
            if (!(heightmap_export_max->value() > value))
              heightmap_export_max->setValue(value + 1.0);

          });

  connect(heightmap_export_max, qOverload<double>(&QDoubleSpinBox::valueChanged),
          [=](double value)
          {
            if (!(heightmap_export_min->value() < value))
              heightmap_export_min->setValue(value - 1.0);

          });

  connect(heightmap_export_params_auto_height, &QPushButton::clicked
      , [=]()
      {
          MapTile* tile = _world->mapIndex.getTile(_camera.position);
          if (tile)
          {
              QSignalBlocker const blocker_min(heightmap_export_min);
              QSignalBlocker const blocker_max(heightmap_export_max);

              heightmap_export_min->setValue(tile->getMinHeight());
              heightmap_export_max->setValue(tile->getMaxHeight());
          }
      });

  connect(heightmap_export_okay, &QPushButton::clicked
    ,[=]()
    {
      heightmap_export_params->accept();

    });



  ADD_ACTION_NS ( cur_adt_export_menu
  , "Export heightmap"
  , [=]
              {
                QPoint new_pos = QCursor::pos();

                heightmap_export_params->setGeometry(new_pos.x(),
                new_pos.y(),
                heightmap_export_params->width(),
                heightmap_export_params->height());

                if (heightmap_export_params->exec() == QDialog::Accepted)
                {
                  makeCurrent();
                  OpenGL::context::scoped_setter const _(::gl, context());

                  _world->exportADTHeightmap(_camera.position, heightmap_export_min->value(), heightmap_export_max->value());
                }

              }
  );

  ADD_ACTION_NS ( cur_adt_export_menu
  , "Export normalmap"
  , [this]
      {
        makeCurrent();
        OpenGL::context::scoped_setter const _(::gl, context());
        _world->exportADTNormalmap(_camera.position);
      }
  );

  auto cur_adt_import_menu(assist_menu->addMenu("Import"));

  // alphamaps import
  auto const alphamap_image_format = "Required Image format :\n1024x1024 and 8bit color channel.";

  QDialog* adt_import_params = new QDialog(this);
  adt_import_params->setWindowFlags(Qt::Popup);
  adt_import_params->setWindowTitle("Alphamap Importer");
  QVBoxLayout* adt_import_params_layout = new QVBoxLayout(adt_import_params);

  adt_import_params_layout->addWidget(new QLabel("Layer:", adt_import_params));
  QSpinBox* adt_import_params_layer = new QSpinBox(adt_import_params);
  adt_import_params_layer->setRange(1, 3);
  adt_import_params_layout->addWidget(adt_import_params_layer);

  QCheckBox* adt_import_params_cleanup_layers = new QCheckBox("Cleanup unused chunk layers", adt_import_params);
  adt_import_params_cleanup_layers->setToolTip("Remove textures that have empty layers from chunks.");
  adt_import_params_cleanup_layers->setChecked(false);
  adt_import_params_layout->addWidget(adt_import_params_cleanup_layers);

  QPushButton* adt_import_params_okay = new QPushButton("Okay", adt_import_params);
  adt_import_params_layout->addWidget(adt_import_params_okay);

  auto const alphamap_file_info_tooltip = "\nThe image file must be placed in the map's directory in the project"
      " folder with the following naming : MAPNAME_XX_YY_layer1.png (or layer2...)."
      "\nFor example \"C:/noggitproject/world/maps/MAPNAME/MAPNAME_29_53_layer2.png\"";
  adt_import_params_okay->setToolTip(alphamap_file_info_tooltip);

  connect(adt_import_params_okay, &QPushButton::clicked
    ,[=]()
    {
      adt_import_params->accept();

    });

  ADD_ACTION_NS ( cur_adt_import_menu
  , "Import alphamap (file)"
  , [=]
                  {
                    QPoint new_pos = QCursor::pos();

                    adt_import_params->setGeometry(new_pos.x(),
                                                   new_pos.y(),
                                                   heightmap_export_params->width(),
                                                   heightmap_export_params->height());

                    if (adt_import_params->exec() == QDialog::Accepted)
                    {
                      makeCurrent();
                      OpenGL::context::scoped_setter const _(::gl, context());

                      QString filepath = QFileDialog::getOpenFileName(
                        this,
                        tr("Open alphamap"),
                        "",
                        "PNG file (*.png);;"
                      );

                      if(!QFileInfo::exists(filepath))
                        return;

                      QImage img;
                      img.load(filepath, "PNG");

                      NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNKS_TEXTURE);
                      _world->importADTAlphamap(_camera.position, img, adt_import_params_layer->value(), adt_import_params_cleanup_layers->isChecked());
                      NOGGIT_ACTION_MGR->endAction();
                    }

                  }
  );

  ADD_ACTION_NS ( cur_adt_import_menu
  , "Import alphamaps"
  , [=]
    {

        makeCurrent();
        OpenGL::context::scoped_setter const _(::gl, context());

        NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNKS_TEXTURE);
        _world->importADTAlphamap(_camera.position, adt_import_params_cleanup_layers->isChecked());
        NOGGIT_ACTION_MGR->endAction();
    }
  );

  auto const heightmap_image_format = "Required Image format :\n257x257 or 256x256(tiled edges)\nand 16bit per color channel.";

  auto const heightmap_file_info_tooltip = "Requires a .png image of 257x257, or 256x256 in Tiled Edges mode.(Otherwise it will be stretched)"
      "\nThe image file must be placed in the map's directory in the project folder with the following naming : MAPNAME_XX_YY_height.png."
      "\nFor example \"C:/noggitproject/world/maps/MAPNAME/MAPNAME_29_53_height.png\"";

  auto const tiled_edges_tooltip_str = "Tiled edge uses a 256x256 image instead 257."
      "\nTiled image imports encroach on edge vertices on neighboring tiles to avoid duplicate edges. ";

  /*auto const multiplier_tooltip_str = "Multiplies pixel values by this to obtain the final position."
      "\n For example a pixel grayscale of 40%(0.4%) with a multiplier of 100 means this vertex's height will be 0.4*100 = 40.";
*/

  // heightmaps
  QDialog* adt_import_height_params = new QDialog(this);
  adt_import_height_params->setWindowFlags(Qt::Popup);
  adt_import_height_params->setWindowTitle("Heightmap Importer");
  QVBoxLayout* adt_import_height_params_layout = new QVBoxLayout(adt_import_height_params);

  adt_import_height_params_layout->addWidget(new QLabel(heightmap_image_format, adt_import_height_params));

  adt_import_height_params_layout->addWidget(new QLabel("Min Height:", adt_import_height_params));
  QDoubleSpinBox* heightmap_import_min = new QDoubleSpinBox(adt_import_height_params);
  heightmap_import_min->setRange(MIN_HEIGHT, MAX_HEIGHT);
  heightmap_import_min->setValue(DEFAULT_MIN_HEIGHT);
  adt_import_height_params_layout->addWidget(heightmap_import_min);

  adt_import_height_params_layout->addWidget(new QLabel("Max Height:", adt_import_height_params));
  QDoubleSpinBox* heightmap_import_max = new QDoubleSpinBox(adt_import_height_params);
  heightmap_import_max->setRange(MIN_HEIGHT, MAX_HEIGHT);
  heightmap_import_max->setValue(DEFAULT_MAX_HEIGHT);
  adt_import_height_params_layout->addWidget(heightmap_import_max);

  QPushButton* adt_import_height_params_auto_height = new QPushButton("Auto Heights", adt_import_height_params);
  adt_import_height_params_auto_height->setToolTip(autoheights_tooltip_str.c_str());
  adt_import_height_params_layout->addWidget(adt_import_height_params_auto_height);

  adt_import_height_params_layout->addWidget(new QLabel("Mode:", adt_import_height_params));
  QComboBox* adt_import_height_params_mode = new QComboBox(adt_import_height_params);
  adt_import_height_params_layout->addWidget(adt_import_height_params_mode);
  adt_import_height_params_mode->addItems({"Set", "Add", "Subtract", "Multiply" });

  QCheckBox* adt_import_height_tiled_edges = new QCheckBox("Tiled Edges", adt_import_height_params);
  adt_import_height_tiled_edges->setToolTip(tiled_edges_tooltip_str);
  adt_import_height_params_layout->addWidget(adt_import_height_tiled_edges);

  QPushButton* adt_import_height_params_okay = new QPushButton("Okay", adt_import_height_params);
  adt_import_height_params_layout->addWidget(adt_import_height_params_okay);
  adt_import_height_params_okay->setToolTip(heightmap_file_info_tooltip);

  connect(adt_import_height_params_auto_height, &QPushButton::clicked
    , [=]()
    {
      MapTile* tile = _world->mapIndex.getTile(_camera.position);
      if (tile)
      {
        heightmap_import_min->setValue(tile->getMinHeight());
        heightmap_import_max->setValue(tile->getMaxHeight());
      }
    });

  connect(adt_import_height_params_okay, &QPushButton::clicked
    ,[=]()
          {
            adt_import_height_params->accept();

          });

  ADD_ACTION_NS ( cur_adt_import_menu
  , "Import heightmap (file)"
  , [=]
      {
        if (adt_import_height_params->exec() == QDialog::Accepted)
        {
          makeCurrent();
          OpenGL::context::scoped_setter const _(::gl, context());

          QString filepath = QFileDialog::getOpenFileName(
            this,
            tr("Open heightmap (257x257)"),
            "",
            "PNG file (*.png);;"
          );

          if(!QFileInfo::exists(filepath))
            return;

          QImage img;
          img.load(filepath, "PNG");

          NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNKS_TERRAIN);
          _world->importADTHeightmap(_camera.position, img, heightmap_import_min->value(), heightmap_import_max->value(),
                                     adt_import_height_params_mode->currentIndex(), adt_import_height_tiled_edges->isChecked());
          NOGGIT_ACTION_MGR->endAction();
        }
      }
  );

  ADD_ACTION_NS ( cur_adt_import_menu
  , "Import heightmap"
  , [=]
      {
        if (adt_import_height_params->exec() == QDialog::Accepted)
        {
          makeCurrent();
          OpenGL::context::scoped_setter const _(::gl, context());

          NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNKS_TERRAIN);
          _world->importADTHeightmap(_camera.position, heightmap_import_min->value(), heightmap_import_max->value(),
                                     adt_import_height_params_mode->currentIndex(), adt_import_height_tiled_edges->isChecked());
          NOGGIT_ACTION_MGR->endAction();
        }
      }
  );

  // Watermap
  QDialog* adt_import_water_params = new QDialog(this);
  adt_import_water_params->setWindowFlags(Qt::Popup);
  adt_import_water_params->setWindowTitle("Watermap Importer");
  QVBoxLayout* adt_import_water_params_layout = new QVBoxLayout(adt_import_water_params);

  // MIN MAX
  adt_import_water_params_layout->addWidget(new QLabel("Min Height:", adt_import_water_params));
  QDoubleSpinBox* watermap_import_min = new QDoubleSpinBox(adt_import_water_params);
  watermap_import_min->setRange(MIN_HEIGHT, MAX_HEIGHT);
  watermap_import_min->setValue(MIN_HEIGHT);
  adt_import_water_params_layout->addWidget(watermap_import_min);

  adt_import_water_params_layout->addWidget(new QLabel("Max Height:", adt_import_water_params));
  QDoubleSpinBox* watermap_import_max = new QDoubleSpinBox(adt_import_water_params);
  watermap_import_max->setRange(MIN_HEIGHT, MAX_HEIGHT);
  watermap_import_max->setValue(MAX_HEIGHT);
  adt_import_water_params_layout->addWidget(watermap_import_max);

  adt_import_water_params_layout->addWidget(new QLabel("Mode:", adt_import_water_params));
  QComboBox* adt_import_water_params_mode = new QComboBox(adt_import_water_params);
  adt_import_water_params_layout->addWidget(adt_import_water_params_mode);
  adt_import_water_params_mode->addItems({ "Set", "Add", "Subtract", "Multiply" });

  QCheckBox* adt_import_water_tiled_edges = new QCheckBox("Tiled Edges", adt_import_water_params);
  adt_import_water_params_layout->addWidget(adt_import_water_tiled_edges);

  QPushButton* adt_import_water_params_okay = new QPushButton("Okay", adt_import_water_params);
  adt_import_water_params_layout->addWidget(adt_import_water_params_okay);

  connect(adt_import_water_params_okay, &QPushButton::clicked
      , [=]()
      {
          adt_import_water_params->accept();

      });

  ADD_ACTION_NS(cur_adt_import_menu
      , "Import watermap (file)"
      , [=]
      {
          if (adt_import_water_params->exec() == QDialog::Accepted)
          {
              makeCurrent();
              OpenGL::context::scoped_setter const _(::gl, context());

              QString filepath = QFileDialog::getOpenFileName(
                  this,
                  tr("Open watermap (257x257)"),
                  "",
                  "PNG file (*.png);;"
              );

              if (!QFileInfo::exists(filepath))
                  return;

              QImage img;
              img.load(filepath, "PNG");

              NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNKS_WATER);
              _world->importADTWatermap(_camera.position, img, watermap_import_min->value(), watermap_import_max->value(),
                  adt_import_water_params_mode->currentIndex(), adt_import_water_tiled_edges->isChecked());
              NOGGIT_ACTION_MGR->endAction();
          }
      }
  );

  // Vertex Colors
  QDialog* adt_import_vcol_params = new QDialog(this);
  adt_import_vcol_params->setWindowFlags(Qt::Popup);
  adt_import_vcol_params->setWindowTitle("Vertex Color Map Importer");
  QVBoxLayout* adt_import_vcol_params_layout = new QVBoxLayout(adt_import_vcol_params);

  adt_import_vcol_params_layout->addWidget(new QLabel("Mode:", adt_import_vcol_params));
  QComboBox* adt_import_vcol_params_mode = new QComboBox(adt_import_vcol_params);
  adt_import_vcol_params_layout->addWidget(adt_import_vcol_params_mode);
  adt_import_vcol_params_mode->addItems({"Set", "Add", "Subtract", "Multiply"});

  QCheckBox* adt_import_vcol_params_mode_tiled_edges = new QCheckBox("Tiled Edges", adt_import_vcol_params);
  adt_import_vcol_params_layout->addWidget(adt_import_vcol_params_mode_tiled_edges);

  QPushButton* adt_import_vcol_params_okay = new QPushButton("Okay", adt_import_vcol_params);
  adt_import_vcol_params_layout->addWidget(adt_import_vcol_params_okay);

  connect(adt_import_vcol_params_okay, &QPushButton::clicked
    ,[=]()
          {
            adt_import_vcol_params->accept();

          });


  ADD_ACTION_NS ( cur_adt_import_menu
  , "Import vertex color map (file)"
  , [=]
    {
      if (adt_import_vcol_params->exec() == QDialog::Accepted)
      {
        makeCurrent();
        OpenGL::context::scoped_setter const _(::gl, context());

        QString filepath = QFileDialog::getOpenFileName(
          this,
          tr("Open vertex color map (257x257)"),
          "",
          "PNG file (*.png);;"
        );

        if(!QFileInfo::exists(filepath))
          return;

        QImage img;
        img.load(filepath, "PNG");

        NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNKS_VERTEX_COLOR);
        _world->importADTVertexColorMap(_camera.position, img, adt_import_vcol_params_mode->currentIndex(), adt_import_vcol_params_mode_tiled_edges->isChecked());
        NOGGIT_ACTION_MGR->endAction();
      }
    }
  );

  ADD_ACTION_NS ( cur_adt_import_menu
  , "Import vertex color map"
  , [=]
      {
        if (adt_import_vcol_params->exec() == QDialog::Accepted)
        {
          makeCurrent();
          OpenGL::context::scoped_setter const _(::gl, context());

          NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNKS_VERTEX_COLOR);
          _world->importADTVertexColorMap(_camera.position, adt_import_vcol_params_mode->currentIndex(), adt_import_vcol_params_mode_tiled_edges->isChecked());
          NOGGIT_ACTION_MGR->endAction();
        }
      }
  );


  assist_menu->addSeparator();
  assist_menu->addAction(createTextSeparator("Loaded ADTs"));
  assist_menu->addSeparator();
  ADD_ACTION_NS ( assist_menu
  , "Fix terrain gaps between chunks"
  , [this]
      {
        makeCurrent();
        OpenGL::context::scoped_setter const _ (::gl, context());
        NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNKS_TERRAIN);
        _world->fixAllGaps();
        NOGGIT_ACTION_MGR->endAction();
      }
  );

  ADD_ACTION_NS(assist_menu
      , "Cleanup empty texture chunks"
      , [this]
      {
          makeCurrent();
          OpenGL::context::scoped_setter const _(::gl, context());
          NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNKS_TEXTURE);
          _world->CleanupEmptyTexturesChunks();
          NOGGIT_ACTION_MGR->endAction();
      }
  );

  assist_menu->addSeparator();
  assist_menu->addAction(createTextSeparator("Global"));
  assist_menu->addSeparator();
  ADD_ACTION_NS ( assist_menu
  , "Convert Map to 8bits alphamaps"
  , [this]
    {
      DESTRUCTIVE_ACTION
      (
        makeCurrent();
        OpenGL::context::scoped_setter const _ (::gl, context());
        if (_world->mapIndex.hasBigAlpha())
        {
            QMessageBox::information(this
                , "Noggit"
                , "Map is already Big Alpha."
                , QMessageBox::Ok
            );
        }
        else
        {
            QProgressDialog progress_dialog("Converting Alpha format...", "", 0, _world->mapIndex.getNumExistingTiles(), this);
            progress_dialog.setWindowModality(Qt::WindowModal);
            _world->convert_alphamap(&progress_dialog, true);
        }
      )
    }
  );

  ADD_ACTION_NS ( assist_menu
  , "Convert Map to 4bits alphamaps (old format)"
  , [this]
    {
      DESTRUCTIVE_ACTION
      (
        makeCurrent();
        OpenGL::context::scoped_setter const _(::gl, context());
        if (!_world->mapIndex.hasBigAlpha())
        {
            QMessageBox::information(this
                , "Noggit"
                , "Map is already Old Alpha."
                , QMessageBox::Ok
            );
        }
        else
        {
            QProgressDialog progress_dialog("Converting Alpha format...", "", 0, _world->mapIndex.getNumExistingTiles(), this);
            _world->convert_alphamap(&progress_dialog, false);
        }
      )
    }
  );


  ADD_ACTION_NS ( assist_menu
  , "Ensure 4 texture layers"
  , [=]
      {
        DESTRUCTIVE_ACTION
        (
          makeCurrent();
          OpenGL::context::scoped_setter const _(::gl, context());
          _world->ensureAllTilesetsAllADTs();
        )

      }
  );

  auto all_adts_export_menu(assist_menu->addMenu("Export"));

  ADD_ACTION_NS ( all_adts_export_menu
  , "Export alphamaps"
  , [this]
    {
      DESTRUCTIVE_ACTION
      (
        makeCurrent();
        OpenGL::context::scoped_setter const _(::gl, context());
        _world->exportAllADTsAlphamap();
      )
    }
  );

  ADD_ACTION_NS ( all_adts_export_menu
  , "Export alphamaps (current texture)"
  , [this]
  {
    DESTRUCTIVE_ACTION
    (
      makeCurrent();
      OpenGL::context::scoped_setter const _(::gl, context());

      if (!!Noggit::Ui::selected_texture::get())
      {
        _world->exportAllADTsAlphamap(Noggit::Ui::selected_texture::get()->get()->file_key().filepath());
      }
    )
  }
  );

  ADD_ACTION_NS ( all_adts_export_menu
  , "Export heightmap"
  , [this]
    {
      DESTRUCTIVE_ACTION
      (
        makeCurrent();
        OpenGL::context::scoped_setter const _(::gl, context());

        _world->exportAllADTsHeightmap();
      )
    }
  );

  ADD_ACTION_NS ( all_adts_export_menu
  , "Export vertex color map"
  , [this]
    {
      DESTRUCTIVE_ACTION
      (
        makeCurrent();
        OpenGL::context::scoped_setter const _(::gl, context());

        _world->exportAllADTsVertexColorMap();
      )
    }
  );

  auto all_adts_import_menu(assist_menu->addMenu("Import"));

  ADD_ACTION_NS ( all_adts_import_menu
  , "Import alphamaps"
  , [this]
  {
    DESTRUCTIVE_ACTION
    (
        makeCurrent();
        OpenGL::context::scoped_setter const _(::gl, context());
        QProgressDialog progress_dialog("Importing Alphamaps...", "Cancel", 0, _world->mapIndex.getNumExistingTiles(), this);
        progress_dialog.setWindowModality(Qt::WindowModal);
        NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNKS_TEXTURE);
        _world->importAllADTsAlphamaps(&progress_dialog);
        NOGGIT_ACTION_MGR->endAction();
    )
  }
  );
  ADD_ACTION_NS ( all_adts_import_menu
  , "Import heightmaps"
  , [=]
    {
      if (adt_import_height_params->exec() == QDialog::Accepted)
      {
        DESTRUCTIVE_ACTION
        (
            makeCurrent();
            OpenGL::context::scoped_setter const _(::gl, context());
            QProgressDialog progress_dialog("Importing Heightmaps...", "Cancel", 0, _world->mapIndex.getNumExistingTiles(), this);
            progress_dialog.setWindowModality(Qt::WindowModal);
            NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNKS_TERRAIN);
            _world->importAllADTsHeightmaps(&progress_dialog, heightmap_import_min->value(), heightmap_import_max->value(), 
                adt_import_height_params_mode->currentIndex(), adt_import_height_tiled_edges->isChecked());
            NOGGIT_ACTION_MGR->endAction();
        )

      }
    }
  );

  ADD_ACTION_NS ( all_adts_import_menu
  , "Import vertex color maps"
  , [=]
  {
    if (adt_import_vcol_params->exec() == QDialog::Accepted)
    {
      DESTRUCTIVE_ACTION
      (
          makeCurrent();
          OpenGL::context::scoped_setter const _(::gl, context());
          NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNKS_VERTEX_COLOR);
          _world->importAllADTVertexColorMaps(adt_import_vcol_params_mode->currentIndex(), adt_import_vcol_params_mode_tiled_edges->isChecked());
          NOGGIT_ACTION_MGR->endAction();
      )

    }
  }
  );

  auto debug_menu(assist_menu->addMenu("Debug"));

  ADD_ACTION_NS ( debug_menu
  , "Load all tiles"
  , [=]
  {
    makeCurrent();
    OpenGL::context::scoped_setter const _(::gl, context());
    _unload_tiles = false;
    _world->loadAllTiles(_camera.position);
  }
  );

}

void MapView::setupViewMenu()
{
  auto view_menu (_main_window->_menuBar->addMenu ("View"));
  connect (this, &QObject::destroyed, view_menu, &QObject::deleteLater);

  view_menu->addSeparator();
  view_menu->addAction(createTextSeparator("Drawing"));
  view_menu->addSeparator();
  ADD_TOGGLE (view_menu, "Doodads",     Qt::Key_F1, _draw_models);
  ADD_TOGGLE (view_menu, "WMO doodads", Qt::Key_F2, _draw_wmo_doodads);
  ADD_TOGGLE (view_menu, "Terrain",     Qt::Key_F3, _draw_terrain);
  ADD_TOGGLE (view_menu, "Water",       Qt::Key_F4, _draw_water);
  ADD_TOGGLE (view_menu, "WMOs",        Qt::Key_F6, _draw_wmo);

  ADD_TOGGLE_POST (view_menu, "Lines", Qt::Key_F7, _draw_lines,
                   [=]
                   {
                     _world->renderer()->getTerrainParamsUniformBlock()->draw_lines = _draw_lines.get();
                     _world->renderer()->markTerrainParamsUniformBlockDirty();
                   });

  ADD_TOGGLE_POST (view_menu, "Contours", Qt::Key_F9, _draw_contour,
                   [=]
                   {
                     _world->renderer()->getTerrainParamsUniformBlock()->draw_terrain_height_contour = _draw_contour.get();
                     _world->renderer()->markTerrainParamsUniformBlockDirty();
                   });

  ADD_TOGGLE_POST (view_menu, "Wireframe", Qt::Key_F10, _draw_wireframe,
                   [=]
                   {
                     _world->renderer()->getTerrainParamsUniformBlock()->draw_wireframe = _draw_wireframe.get();
                     _world->renderer()->markTerrainParamsUniformBlockDirty();
                   });

  ADD_TOGGLE (view_menu, "Toggle Animation", Qt::Key_F11, _draw_model_animations);
  ADD_TOGGLE (view_menu, "Draw fog", Qt::Key_F12, _draw_fog);

  ADD_TOGGLE_POST (view_menu, "Hole lines", Qt::SHIFT | Qt::Key_F1, _draw_hole_lines,
                   [=]
                   {
                     _world->renderer()->getTerrainParamsUniformBlock()->draw_hole_lines = _draw_hole_lines.get();
                     _world->renderer()->markTerrainParamsUniformBlockDirty();
                   });

  ADD_TOGGLE_POST(view_menu, "Climb", Qt::SHIFT | Qt::Key_F2, _draw_climb,
                  [=]
                  {
                      _world->renderer()->getTerrainParamsUniformBlock()->draw_impassible_climb = _draw_climb.get();
                      _world->renderer()->markTerrainParamsUniformBlockDirty();
                  });

  ADD_TOGGLE_POST(view_menu, "Vertex Color", Qt::SHIFT | Qt::Key_F3, _draw_vertex_color,
      [=]
      {
          _world->renderer()->getTerrainParamsUniformBlock()->draw_vertex_color = _draw_vertex_color.get();
          _world->renderer()->markTerrainParamsUniformBlockDirty();
      });

  ADD_TOGGLE_POST(view_menu, "Tileset", Qt::SHIFT | Qt::Key_F5, _draw_tileset,
      [=]
      {
          _world->renderer()->getTerrainParamsUniformBlock()->draw_tileset = _draw_tileset.get();
          _world->renderer()->markTerrainParamsUniformBlockDirty();
      });

  ADD_TOGGLE_POST(view_menu, "Baked Shadows", Qt::SHIFT | Qt::Key_F4, _draw_baked_shadows,
      [=]
      {
          _world->renderer()->getTerrainParamsUniformBlock()->draw_shadows = _draw_baked_shadows.get();
          _world->renderer()->markTerrainParamsUniformBlockDirty();
      });

  ADD_TOGGLE_NS (view_menu, "Flight Bounds", _draw_mfbo);

  ADD_TOGGLE_NS (view_menu, "Models with box", _draw_models_with_box);
  //! \todo space+h in object mode
  ADD_TOGGLE_NS (view_menu, "Hidden models", _draw_hidden_models);

  ADD_TOGGLE_NS(view_menu, "Draw Sky", _draw_sky);
  ADD_TOGGLE_NS(view_menu, "Draw Skybox", _draw_skybox);

  auto debug_menu (view_menu->addMenu ("Debug"));
  ADD_TOGGLE_NS (debug_menu, "Occlusion boxes", _draw_occlusion_boxes);

  view_menu->addSeparator();
  view_menu->addAction(createTextSeparator("Tools"));
  view_menu->addSeparator();

  ADD_TOGGLE (view_menu, "Show Node Editor", "Shift+N", _show_node_editor);

  // ADD_TOGGLE_NS(view_menu, "Game View", _game_mode_camera);

  view_menu->addSeparator();
  view_menu->addAction(createTextSeparator("Minimap"));
  view_menu->addSeparator();

  ADD_TOGGLE (view_menu, "Show", Qt::Key_M, _show_minimap_window);


  ADD_TOGGLE_NS(view_menu, "Show ADT borders", _show_minimap_borders);

  ADD_TOGGLE_NS(view_menu, "Show light zones", _show_minimap_skies);

  view_menu->addSeparator();
  view_menu->addAction(createTextSeparator("Windows"));
  view_menu->addSeparator();

  auto hide_widgets = [=]
  {

    QWidget *widget_list[] =
      {
        _detail_infos_dock,
        _keybindings,
        _minimap_dock,
        _asset_browser_dock,
        _overlay_widget,
        _tool_panel_dock

      };

    if (_main_window->displayed_widgets.empty())
    {
      for (auto widget : widget_list)
        if (widget->isVisible())
        {
          _main_window->displayed_widgets.emplace(widget);
          widget->hide();
        }

    }
    else
    {
      for (auto widget : _main_window->displayed_widgets)
        widget->show();

      _main_window->displayed_widgets.clear();
    }


    _main_window->statusBar()->setVisible(ui_hidden);
    _toolbar->setVisible(ui_hidden);
    _view_toolbar->setVisible(ui_hidden);

    ui_hidden = !ui_hidden;

    setToolPropertyWidgetVisibility(terrainMode);

  };

  ADD_ACTION(view_menu, "Toggle UI", Qt::Key_Tab, hide_widgets);

  ADD_TOGGLE (view_menu, "Detail infos", Qt::Key_F8, _show_detail_info_window);

  addHotkey( Qt::Key_H
    , MOD_none
    , [this] { activeTool()->onHotkeyPress("toggleTexturePalette"_hash); }
    , [this] { return activeTool()->hotkeyCondition("toggleTexturePalette"_hash); }
  );

  ADD_ACTION (view_menu, "Increase time speed", Qt::Key_N, [this] { mTimespeed += 90.0f; });
  ADD_ACTION (view_menu, "Decrease time speed", Qt::Key_B, [this] { mTimespeed = std::max (0.0f, mTimespeed - 90.0f); });
  ADD_ACTION (view_menu, "Pause time", Qt::Key_J, [this] { mTimespeed = 0.0f; });
  ADD_ACTION (view_menu, "Invert mouse", "I", [this] { mousedir *= -1.f; });
  ADD_ACTION (view_menu, "Decrease camera speed", Qt::Key_O, [this] { _camera.move_speed *= 0.5f; });
  ADD_ACTION (view_menu, "Increase camera speed", Qt::Key_P, [this] { _camera.move_speed *= 2.0f; });
  ADD_ACTION ( view_menu
  , "Turn camera around 180°"
  , "Shift+R"
  , [this]
               {
                 _camera.add_to_yaw(math::degrees(180.f));
                 _camera_moved_since_last_draw = true;
               }
  );

  ADD_ACTION ( view_menu
  , "Toggle tile mode"
  , Qt::Key_U
  , [this]
               {
                 if (NOGGIT_CUR_ACTION)
                   return;

                 if (_display_mode == display_mode::in_2D)
                 {
                   _display_mode = display_mode::in_3D;
                   set_editing_mode (saveterrainMode);
                 }
                 else
                 {
                   _display_mode = display_mode::in_2D;
                   saveterrainMode = terrainMode;
                   set_editing_mode (editing_mode::paint);
                 }
               }
  );

  view_menu->addSeparator();
  view_menu->addAction(createTextSeparator("Camera Modes"));
  view_menu->addSeparator();

  /* // TODO, doesn't work for some reason.
  ADD_TOGGLE_NS(view_menu, "Debug cam", _debug_cam_mode);
  connect(&_debug_cam_mode, &Noggit::BoolToggleProperty::changed
      , [this]
      {
          _debug_cam = Noggit::Camera(_camera.position, _camera.yaw(), _camera.pitch());
      }
  );

  ADD_ACTION_NS(view_menu
      , "Go to debug camera"
      , [this]
      {
          _camera = Noggit::Camera(_debug_cam.position, _debug_cam.yaw(), _debug_cam.pitch());
      }
  );*/

  ADD_TOGGLE_NS(view_menu, "FPS camera", _fps_mode);
  connect(&_fps_mode, &Noggit::BoolToggleProperty::changed
    , [this]
    {
      setCameraDirty();
      auto ground_pos = getWorld()->get_ground_height(getCamera()->position);
      getCamera()->position.y = ground_pos.y + 2;
    }
  );

  ADD_TOGGLE_NS(view_menu, "Camera Collision", _camera_collision);

}

void MapView::setupToolsMenu()
{
    auto menu(_main_window->_menuBar->addMenu("Tools"));
    connect(this, &QObject::destroyed, menu, &QObject::deleteLater);

    for (auto&& tool : _tools)
    {
        tool->registerMenuItems(menu);
    }
}

void MapView::setupHelpMenu()
{
  auto help_menu (_main_window->_menuBar->addMenu ("Help"));
  connect (this, &QObject::destroyed, help_menu, &QObject::deleteLater);

  ADD_TOGGLE (help_menu, "Key Bindings", "Ctrl+F1", _show_keybindings_window);

#if defined(_WIN32) || defined(WIN32)
  ADD_ACTION_NS ( help_menu
                , "WoW Modding Discord"
                , []
                  {
                    ShellExecute ( nullptr
                                 , "open"
                                 , "https://discord.gg/Dnrztg7dCZ"
                                 , nullptr
                                 , nullptr
                                 , SW_SHOWNORMAL
                                 );
                  }
                );
  ADD_ACTION_NS ( help_menu
                , "Noggit Red Repository"
                , []
                  {
                    ShellExecute ( nullptr
                                 , "open"
                                 , "https://gitlab.com/prophecy-rp/noggit-red/"
                                 , nullptr
                                 , nullptr
                                 , SW_SHOWNORMAL
                                 );
                  }
                );

  ADD_ACTION_NS ( help_menu
                , "Noggit Red Discord"
                , []
                  {
                    ShellExecute ( nullptr
                                 , "open"
                                 , "https://discord.gg/Tk2TpN8CaF"
                                 , nullptr
                                 , nullptr
                                 , SW_SHOWNORMAL
                                 );
                  }
                );
#endif

}

void MapView::setupClientMenu()
{
  // can add this to main menu instead in NoggitWindow()

  auto client_menu(_main_window->_menuBar->addMenu("Client"));
  connect(this, &QObject::destroyed, client_menu, &QObject::deleteLater); // to remove from main menu

  // ADD_ACTION_NS(client_menu, "Start Client",  [this] { _main_window->startWowClient(); });
  auto start_client_action(client_menu->addAction("Start Client"));
  connect(start_client_action, &QAction::triggered, [this] { _main_window->startWowClient(); });

  // ADD_ACTION_NS(client_menu, "Patch Client", [this] { _main_window->patchWowClient(); });
  auto pack_client_action(client_menu->addAction("Patch Client"));
  pack_client_action->setToolTip("Save content of project folder as MPQ patch in the client.");
  connect(pack_client_action, &QAction::triggered, [this] { _main_window->patchWowClient(); });

}

void MapView::setupHotkeys()
{

  addHotkey ( Qt::Key_F1
    , MOD_shift
    , [this]
              {
                if (alloff)
                {
                  alloff_models = _draw_models.get();
                  alloff_doodads = _draw_wmo_doodads.get();
                  alloff_contour = _draw_contour.get();
                  alloff_climb = _draw_climb.get();
                  alloff_vertex_color = _draw_vertex_color.get();
                  alloff_tileset = _draw_tileset.get();
                  alloff_baked_shadows = _draw_baked_shadows.get();
                  alloff_wmo = _draw_wmo.get();
                  alloff_fog = _draw_fog.get();
                  alloff_terrain = _draw_terrain.get();

                  _draw_models.set (false);
                  _draw_wmo_doodads.set (false);
                  _draw_contour.set (true);
                  _draw_climb.set (false);
                  _draw_vertex_color.set(true);
                  _draw_tileset.set(true);
                  _draw_baked_shadows.set(false);
                  _draw_wmo.set (false);
                  _draw_terrain.set (true);
                  _draw_fog.set (false);
                }
                else
                {
                  _draw_models.set (alloff_models);
                  _draw_wmo_doodads.set (alloff_doodads);
                  _draw_contour.set (alloff_contour);
                  _draw_climb.set(alloff_climb);
                  _draw_vertex_color.set(alloff_vertex_color);
                  _draw_tileset.set(alloff_tileset);
                  _draw_baked_shadows.set(alloff_baked_shadows);
                  _draw_wmo.set (alloff_wmo);
                  _draw_terrain.set (alloff_terrain);
                  _draw_fog.set (alloff_fog);
                }
                alloff = !alloff;
              }
  );

  addHotkey(Qt::Key_C, MOD_ctrl, "copySelection"_hash);

  addHotkey(Qt::Key_V, MOD_ctrl, "paste"_hash);

  addHotkey(Qt::Key_V, MOD_shift, "importM2FromWmv"_hash);

  addHotkey(Qt::Key_V, MOD_alt, "importWmoFromWmv"_hash);

  addHotkey(Qt::Key_C, MOD_none, "clearVertexSelection"_hash);

  addHotkey(Qt::Key_B, MOD_ctrl, "duplacteSelection"_hash);

  addHotkey(Qt::Key_Y, MOD_none, "nextType"_hash);

  addHotkey(Qt::Key_T, MOD_none, "toggleAngle"_hash);

  addHotkey(Qt::Key_T, MOD_space, "nextMode"_hash);

  addHotkey(Qt::Key_T, MOD_none, "toggleTool"_hash);

  addHotkey(Qt::Key_T, MOD_none, "unsetAdtHole"_hash);
  addHotkey(Qt::Key_T, MOD_alt, "setAdtHole"_hash);

  addHotkey(Qt::Key_T, MOD_none, "toggleAngled"_hash);

  addHotkey(Qt::Key_T, MOD_none, "togglePasteMode"_hash);

  addHotkey ( Qt::Key_H
    , MOD_none
    , [&]
              {
                if (_world->has_selection())
                {
                  for (auto& selection : _world->current_selection())
                  {
                    if (selection.index() != eEntry_Object)
                      continue;

                    auto obj = std::get<selected_object_type>(selection);

                    if (obj->which() == eMODEL)
                    {
                      static_cast<ModelInstance*>(obj)->model->toggle_visibility();
                    }
                    else if (obj->which() == eWMO)
                    {
                      static_cast<WMOInstance*>(obj)->wmo->toggle_visibility();
                    }
                  }
                }
              }
    , [&] { return terrainMode == editing_mode::object && !NOGGIT_CUR_ACTION; }
  );

  addHotkey ( Qt::Key_H
    , MOD_space
    , [&]
              {
                _draw_hidden_models.toggle();
              }
    , [&] { return terrainMode == editing_mode::object && !NOGGIT_CUR_ACTION; }
  );

  addHotkey(Qt::Key_R, MOD_space, "setBrushLevelMinMax"_hash);

  addHotkey ( Qt::Key_H
    , MOD_shift
    , [&]
              {
                ModelManager::clear_hidden_models();
                WMOManager::clear_hidden_wmos();
              }
    , [&] { return terrainMode == editing_mode::object && !NOGGIT_CUR_ACTION; }
  );

  addHotkey(Qt::Key_F, MOD_space, "toggleLock"_hash);

  addHotkey(Qt::Key_F, MOD_none, "lockCursor"_hash);

  addHotkey ( Qt::Key_F
    , MOD_none
    , [&]
              {

                NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eOBJECTS_TRANSFORMED);
                _world->set_selected_models_pos(_cursor_pos);
                emit rotationChanged();
                NOGGIT_ACTION_MGR->endAction();
              }
    , [&] { return terrainMode == editing_mode::object && !NOGGIT_CUR_ACTION; }
  );

  addHotkey(Qt::Key_Plus, MOD_alt, "increaseRadius"_hash);
  addHotkey(Qt::Key_Minus, MOD_alt, "decreaseRadius"_hash);

  addHotkey (Qt::Key_1, MOD_shift, [this] { _camera.move_speed = 15.0f; });
  addHotkey (Qt::Key_2, MOD_shift, [this] { _camera.move_speed = 50.0f; });
  addHotkey (Qt::Key_3, MOD_shift, [this] { _camera.move_speed = 200.0f; });
  addHotkey (Qt::Key_4, MOD_shift, [this] { _camera.move_speed = 800.0f; });

  addHotkey(Qt::Key_1, MOD_alt, "setBrushLevel0Pct"_hash);
  addHotkey(Qt::Key_2, MOD_alt, "setBrushLevel25Pct"_hash);
  addHotkey(Qt::Key_3, MOD_alt, "setBrushLevel50Pct"_hash);
  addHotkey(Qt::Key_4, MOD_alt, "setBrushLevel75Pct"_hash);
  addHotkey(Qt::Key_5, MOD_alt, "setBrushLevel100Pct"_hash);

  addHotkey(Qt::Key_1, MOD_none, [this] { set_editing_mode(editing_mode::ground); }
    , [this] { return !_mod_num_down && !NOGGIT_CUR_ACTION;  });
  addHotkey (Qt::Key_2, MOD_none, [this] { set_editing_mode (editing_mode::flatten_blur); }
    , [this] { return !_mod_num_down && !NOGGIT_CUR_ACTION;  });
  addHotkey (Qt::Key_3, MOD_none, [this] { set_editing_mode (editing_mode::paint); }
    , [this] { return !_mod_num_down && !NOGGIT_CUR_ACTION;  });
  addHotkey (Qt::Key_4, MOD_none, [this] { set_editing_mode (editing_mode::holes); }
    , [this] { return !_mod_num_down && !NOGGIT_CUR_ACTION;  });
  addHotkey (Qt::Key_5, MOD_none, [this] { set_editing_mode (editing_mode::areaid); }
    , [this] { return !_mod_num_down && !NOGGIT_CUR_ACTION;  });
  addHotkey (Qt::Key_6, MOD_none, [this] { set_editing_mode (editing_mode::impass); }
    , [this] { return !_mod_num_down && !NOGGIT_CUR_ACTION;  });
  addHotkey (Qt::Key_7, MOD_none, [this] { set_editing_mode (editing_mode::water); }
    , [this] { return !_mod_num_down && !NOGGIT_CUR_ACTION;  });
  addHotkey (Qt::Key_8, MOD_none, [this] { set_editing_mode (editing_mode::mccv); }
    , [this] { return !_mod_num_down && !NOGGIT_CUR_ACTION;  });
  addHotkey (Qt::Key_9, MOD_none, [this] { set_editing_mode (editing_mode::object); }
    , [this] { return !_mod_num_down && !NOGGIT_CUR_ACTION;  });

  addHotkey(Qt::Key_0, MOD_ctrl, [this] { change_selected_wmo_doodadset(0); });
  addHotkey(Qt::Key_1, MOD_ctrl, [this] { change_selected_wmo_doodadset(1); });
  addHotkey(Qt::Key_2, MOD_ctrl, [this] { change_selected_wmo_doodadset(2); });
  addHotkey(Qt::Key_3, MOD_ctrl, [this] { change_selected_wmo_doodadset(3); });
  addHotkey(Qt::Key_4, MOD_ctrl, [this] { change_selected_wmo_doodadset(4); });
  addHotkey(Qt::Key_5, MOD_ctrl, [this] { change_selected_wmo_doodadset(5); });
  addHotkey(Qt::Key_6, MOD_ctrl, [this] { change_selected_wmo_doodadset(6); });
  addHotkey(Qt::Key_7, MOD_ctrl, [this] { change_selected_wmo_doodadset(7); });
  addHotkey(Qt::Key_8, MOD_ctrl, [this] { change_selected_wmo_doodadset(8); });
  addHotkey(Qt::Key_9, MOD_ctrl, [this] { change_selected_wmo_doodadset(9); });

  addHotkey(Qt::Key_Escape, MOD_none, [this] { _main_window->close(); });

  addHotkey(Qt::Key_Plus, MOD_none, "addColor"_hash);

  addHotkey(Qt::Key_2, MOD_num, "moveSelectedDown"_hash);
  addHotkey(Qt::Key_8, MOD_num, "moveSelectedUp"_hash);
  addHotkey(Qt::Key_4, MOD_num, "moveSelectedLeft"_hash);
  addHotkey(Qt::Key_6, MOD_num, "moveSelectedRight"_hash);

  addHotkey(Qt::Key_3, MOD_num, "rotateSelectedPitchCcw"_hash);
  addHotkey(Qt::Key_1, MOD_num, "rotateSelectedPitchCw"_hash);

  addHotkey(Qt::Key_7, MOD_num, "rotateSelectedYawCcw"_hash);
  addHotkey(Qt::Key_9, MOD_num, "rotateSelectedYawCw"_hash);

  addHotkey(Qt::Key_Plus, MOD_num, "increaseSelectedScale"_hash);
  addHotkey(Qt::Key_Minus, MOD_num, "decreaseSelectedScale"_hash);

  addHotkey(Qt::Key_F, MOD_none, "setAreaId"_hash);

  addHotkey(Qt::Key_Delete, MOD_none, "deleteSelection"_hash);

  addHotkey(Qt::Key_V, MOD_none, "chunkManipulatorPaste"_hash);
}

void MapView::setupMinimap()
{
  _minimap = new Noggit::Ui::minimap_widget(this);
  _minimap_dock = new QDockWidget("Minimap", this);
  _minimap_dock->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
  _minimap_dock->setFixedSize(_minimap->sizeHint());
  _minimap_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

  _minimap->world (_world.get());
  _minimap->camera (&_camera);
  _minimap->draw_boundaries (_show_minimap_borders.get());
  _minimap->draw_skies (_show_minimap_skies.get());
  _minimap->set_resizeable(true);

  connect ( _minimap, &Noggit::Ui::minimap_widget::map_clicked
    , [this] (glm::vec3 const& pos)
            {
              move_camera_with_auto_height (pos);
            }
  );

  _minimap_dock->setFeatures ( QDockWidget::DockWidgetMovable
                               | QDockWidget::DockWidgetFloatable
                               | QDockWidget::DockWidgetClosable
  );
  auto minimap_scroll_area = new QScrollArea(_minimap_dock);
  minimap_scroll_area->setWidget(_minimap);
  minimap_scroll_area->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);

  _minimap_dock->setWidget(minimap_scroll_area);
  _main_window->addDockWidget (Qt::LeftDockWidgetArea, _minimap_dock);
  _minimap_dock->setVisible (false);
  _minimap_dock->setFloating(true);
  _minimap_dock->move(_main_window->rect().center() - _minimap->rect().center());


  connect(this, &QObject::destroyed, _minimap_dock, &QObject::deleteLater);
  connect(this, &QObject::destroyed, _minimap, &QObject::deleteLater);

  connect ( &_show_minimap_window, &Noggit::BoolToggleProperty::changed
    , _minimap_dock, [this]
            {
              if (!ui_hidden)
                _minimap_dock->setVisible(_show_minimap_window.get());
            }
  );


  connect ( _minimap_dock, &QDockWidget::visibilityChanged
    , &_show_minimap_window, &Noggit::BoolToggleProperty::set
  );

  connect ( &_show_minimap_borders, &Noggit::BoolToggleProperty::changed
    , [this]
            {
              _minimap->draw_boundaries(_show_minimap_borders.get());
            }
  );

  connect ( &_show_minimap_skies, &Noggit::BoolToggleProperty::changed
    , [this]
            {
              _minimap->draw_skies(_show_minimap_skies.get());
            }
  );

}

void MapView::createGUI()
{
  // Combined dock
  _tool_panel_dock = new Noggit::Ui::Tools::ToolPanel(this);
  _tool_panel_dock->setFeatures(QDockWidget::DockWidgetMovable
                                | QDockWidget::DockWidgetFloatable);
  _tool_panel_dock->setAllowedAreas(Qt::RightDockWidgetArea | Qt::LeftDockWidgetArea);

  connect(this, &QObject::destroyed, _tool_panel_dock, &QObject::deleteLater);
  _main_window->addDockWidget(Qt::RightDockWidgetArea, _tool_panel_dock);

  setupAssetBrowser();

  _tools.emplace_back(std::make_unique<Noggit::RaiseLowerTool>(this))->setupUi(_tool_panel_dock);
  _tools.emplace_back(std::make_unique<Noggit::FlattenBlurTool>(this))->setupUi(_tool_panel_dock);
  _tools.emplace_back(std::make_unique<Noggit::TexturingTool>(this))->setupUi(_tool_panel_dock);
  _tools.emplace_back(std::make_unique<Noggit::HoleTool>(this))->setupUi(_tool_panel_dock);
  _tools.emplace_back(std::make_unique<Noggit::AreaTool>(this))->setupUi(_tool_panel_dock);
  _tools.emplace_back(std::make_unique<Noggit::ImpassTool>(this))->setupUi(_tool_panel_dock);
  _tools.emplace_back(std::make_unique<Noggit::WaterTool>(this))->setupUi(_tool_panel_dock);
  _tools.emplace_back(std::make_unique<Noggit::VertexPainterTool>(this))->setupUi(_tool_panel_dock);
  _tools.emplace_back(std::make_unique<Noggit::ObjectTool>(this))->setupUi(_tool_panel_dock);
  _tools.emplace_back(std::make_unique<Noggit::MinimapTool>(this))->setupUi(_tool_panel_dock);
  _tools.emplace_back(std::make_unique<Noggit::StampTool>(this))->setupUi(_tool_panel_dock);
  _tools.emplace_back(std::make_unique<Noggit::LightTool>(this))->setupUi(_tool_panel_dock);
  _tools.emplace_back(std::make_unique<Noggit::PointLightTool>(this))->setupUi(_tool_panel_dock);
  _tools.emplace_back(std::make_unique<Noggit::ScriptingTool>(this))->setupUi(_tool_panel_dock);
  _tools.emplace_back(std::make_unique<Noggit::ChunkTool>(this))->setupUi(_tool_panel_dock);
  _tools.emplace_back(std::make_unique<Noggit::AreaTriggerTool>(this))->setupUi(_tool_panel_dock);

  // End combined dock

  setupViewportOverlay();
  // texturingTool->setup_ge_tool_renderer();
  setupNodeEditor();
  setupDetailInfos();
  setupToolbars();
  setupKeybindingsGui();

  setupMinimap();
  setupFileMenu();
  setupEditMenu();
  setupViewMenu();
  setupToolsMenu();
  setupAssistMenu();
  setupHelpMenu();
  setupClientMenu();
  setupHotkeys();

  setupMainToolbar();

  for (auto&& tool : _tools)
  {
      tool->postUiSetup();
  }

  connect(_main_window, &Noggit::Ui::Windows::NoggitWindow::exitPromptOpened, this, &MapView::on_exit_prompt);

  set_editing_mode (editing_mode::ground);

  // do we need to do this every tick ?
#ifdef USE_MYSQL_UID_STORAGE
  if (_settings->value("project/mysql/enabled").toBool())
  {
      if (mysql::hasMaxUIDStoredDB(_world->getMapID()))
      {
        _status_database->setText("MySQL UID sync enabled: "
            + _settings->value("project/mysql/server").toString() + ":"
            + _settings->value("project/mysql/port").toString());
      }
  }
#endif
}

void MapView::on_exit_prompt()
{
  // hide all popups
  _keybindings->hide();
  _minimap_dock->hide();
  _detail_infos_dock->hide();
}

MapView::MapView( math::degrees camera_yaw0
                , math::degrees camera_pitch0
                , glm::vec3 camera_pos
                , Noggit::Ui::Windows::NoggitWindow* NoggitWindow
				        , std::shared_ptr<Noggit::Project::NoggitProject> Project
                , std::unique_ptr<World> world
                , uid_fix_mode uid_fix
                , bool from_bookmark
                )
  : _camera (camera_pos, camera_yaw0, camera_pitch0)
  , mTimespeed(0.0f)
  , _uid_fix (uid_fix)
  , _from_bookmark (from_bookmark)
  , _settings (new QSettings (this))
  , cursor_color (1.f, 1.f, 1.f, 1.f)
  , _cursorType{CursorType::CIRCLE}
  , _main_window (NoggitWindow)
  , _debug_cam(camera_pos, camera_yaw0, camera_pitch0)
  , _world (std::move (world))
  , _status_position (new QLabel (this))
  , _status_selection (new QLabel (this))
  , _status_area (new QLabel (this))
  , _status_time (new QLabel (this))
  , _status_fps (new QLabel (this))
  , _status_culling (new QLabel (this))
  , _status_database(new QLabel(this))
  , _texBrush{new OpenGL::texture{}}
  , _transform_gizmo(Noggit::Ui::Tools::ViewportGizmo::GizmoContext::MAP_VIEW)
  , _tablet_manager(Noggit::TabletManager::instance()),
    _project(Project)
{
  using clock_t = std::chrono::steady_clock;
  auto const t0 = clock_t::now();
  LogError << "MapView::MapView: begin" << std::endl;

  // Bruteforce a stable QOpenGLWidget format before the context is created.
  // Disable MSAA to avoid format/driver instability during early uploads.
  {
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(4, 1);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(0);
    setFormat(fmt);
  }

  setWindowTitle ("Noggit Studio Red - " STRPRODUCTVER);
  setFocusPolicy (Qt::StrongFocus);
  setMouseTracking (true);
  setMinimumHeight(200);
  setMaximumHeight(10000);
  setAttribute(Qt::WA_OpaquePaintEvent, true);
  setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

  _world->LoadSavedSelectionGroups(); // not doing this in world constructor because noggit loads world twice
  LogError << "MapView::MapView: after LoadSavedSelectionGroups (ms="
           << std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - t0).count()
           << ")" << std::endl;

  _context = Noggit::NoggitRenderContext::MAP_VIEW;
  _transform_gizmo.setWorld(_world.get());

  _main_window->setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
  _main_window->setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
  _main_window->setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
  _main_window->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

  _main_window->statusBar()->addWidget (_status_position);
  connect ( this
          , &QObject::destroyed
          , _main_window
          , [=] { _main_window->statusBar()->removeWidget (_status_position); }
          );
  _main_window->statusBar()->addWidget (_status_selection);
  connect ( this
          , &QObject::destroyed
          , _main_window
          , [=] { _main_window->statusBar()->removeWidget (_status_selection); }
          );
  _main_window->statusBar()->addWidget (_status_area);
  connect ( this
          , &QObject::destroyed
          , _main_window
          , [=] { _main_window->statusBar()->removeWidget (_status_area); }
          );
  _main_window->statusBar()->addWidget (_status_time);
  connect ( this
          , &QObject::destroyed
          , _main_window
          , [=] { _main_window->statusBar()->removeWidget (_status_time); }
          );
  _main_window->statusBar()->addWidget (_status_fps);
  connect ( this
          , &QObject::destroyed
          , _main_window
          , [=] { _main_window->statusBar()->removeWidget (_status_fps); }
          );
  _main_window->statusBar()->addWidget (_status_culling);
  connect ( this
      , &QObject::destroyed
      , _main_window
      , [=] { _main_window->statusBar()->removeWidget (_status_culling); }
  );
  _main_window->statusBar()->addWidget(_status_database);
  connect(this
      , &QObject::destroyed
      , _main_window
      , [=] { _main_window->statusBar()->removeWidget(_status_database); }
  );

  setContextMenuPolicy(Qt::CustomContextMenu);

  connect(this, SIGNAL(customContextMenuRequested(const QPoint&)),
      this, SLOT(ShowContextMenu(const QPoint&)));

  connect(this, &MapView::selectionUpdated, [this](std::vector<selection_type>&)
      {
          // updateDetailInfos();
      });

  moving = strafing = updown = lookat = turn = 0.0f;

  freelook = false;

  mousedir = -1.0f;

  look = false;
  _display_mode = display_mode::in_3D;

  _startup_time.start();

  _discord_rich_presence_enabled = _settings->value("integrations/discord_rich_presence", false).toBool();
  _discord_rich_presence_app_id = _settings->value("integrations/discord_app_id", "959654402141085748").toString().toStdString();
  _discord_rich_presence_large_image_key = _settings->value("integrations/discord_large_image_key", "").toString().toStdString();
  _discord_rich_presence_large_image_text = _settings->value("integrations/discord_large_image_text", "Noggit Red").toString().toStdString();
  _discord_rich_presence_start_ts = std::nullopt;
  _discord_last_map.clear();
  _discord_last_tool.clear();
  Noggit::Integrations::DiscordRichPresence::instance().configure(_discord_rich_presence_enabled, _discord_rich_presence_app_id);
  Noggit::Integrations::DiscordRichPresence::instance().setAssets(
    Noggit::Integrations::DiscordAssets{ _discord_rich_presence_large_image_key, _discord_rich_presence_large_image_text }
  );

  int _fps_limit = _settings->value("fps_limit", 60).toInt();
  int _frametime = static_cast<int>((1.f / static_cast<float>(_fps_limit)) * 1000.f);
  std::cout << "FPS limit is set to : " << _fps_limit << " (" << _frametime << ")" << std::endl;

  _update_every_event_loop.start (_frametime);
  connect(&_update_every_event_loop, &QTimer::timeout,[=]
      { 
          _needs_redraw = true;

          Qt::ApplicationState app_state = QGuiApplication::applicationState();
          if (app_state == Qt::ApplicationState::ApplicationSuspended)
          {
              _needs_redraw = false;
              return;
          };

          if (_main_window->isMinimized() && _settings->value("background_fps_limit", true).toBool())
          {
              _needs_redraw = false;
              // return;
          }

          update();
      });

  _point_light_property_undo_timer.setSingleShot(true);
  connect(&_point_light_property_undo_timer, &QTimer::timeout, this, [this] {
    flushPointLightPropertyUndoBatch();
  });

  // reduce frame rate in background
  connect(QGuiApplication::instance(), SIGNAL(applicationStateChanged(Qt::ApplicationState)),
      this, SLOT(onApplicationStateChanged(Qt::ApplicationState)));

  LogError << "MapView::MapView: before createGUI (ms="
           << std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - t0).count()
           << ")" << std::endl;
  createGUI();
  LogError << "MapView::MapView: after createGUI (ms="
           << std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - t0).count()
           << ")" << std::endl;

  connect(&_draw_tileset, &Noggit::BoolToggleProperty::changed, this, [this](bool)
  {
    _world->renderer()->getTerrainParamsUniformBlock()->draw_tileset = _draw_tileset.get();
    _world->renderer()->markTerrainParamsUniformBlockDirty();
  });

  connect(&_draw_texture_layer_count_overlay, &Noggit::BoolToggleProperty::changed, this, [this](bool)
  {
    _world->renderer()->getTerrainParamsUniformBlock()->draw_texture_layer_count_overlay =
      _draw_texture_layer_count_overlay.get();
    _world->renderer()->markTerrainParamsUniformBlockDirty();
  });

  if (QCoreApplication* app = QCoreApplication::instance())
    app->installEventFilter(this);

  _tablet_pressure_strength = _settings->value("tablet/pressure_strength", true).toBool();

  LogError << "MapView::MapView: end (ms="
           << std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - t0).count()
           << ")" << std::endl;
}

void MapView::tabletEvent(QTabletEvent* event)
{
  _tablet_manager->setPressure(event->pressure());
  _tablet_manager->setIsActive(true);
  event->ignore();
}

auto MapView::setBrushTexture(QImage const* img) -> void
{

  int const height{img->height()};
  int const width{img->width()};

  std::vector<std::uint32_t> tex(height * width);

  for(int i{}; i < height; ++i)
    for(int j{}; j < width; ++j)
      tex[i * width + j] = img->pixel(j, i);

  makeCurrent();
  OpenGL::context::scoped_setter const _{gl, context()};
  OpenGL::texture::set_active_texture(4);
  _texBrush->bind();
  gl.texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex.data());
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

Noggit::Camera* MapView::getCamera()
{
  return &_camera;
}

void MapView::move_camera_with_auto_height (glm::vec3 const& pos)
{
  makeCurrent();
  OpenGL::context::scoped_setter const _ (::gl, context());

  TileIndex tile_index = TileIndex(pos);
  if (_world->mapIndex.hasTile(tile_index))
  {
    _world->mapIndex.loadTile(pos)->wait_until_loaded();
  }

  _camera.position = pos;
  _camera.position.y = 0.0f;

  _world->GetVertex (pos.x, pos.z, &_camera.position);

  // min elevation according to https://wowdev.wiki/AreaTable.dbc
  //! \ todo use the current area's MinElevation
  if (_camera.position.y < -5000.0f)
  {
    //! \todo use the height of a model/wmo of the tile (or the map) ?
    _camera.position.y = 0.0f;
  }

  _camera.position.y += 50.0f;

  _camera_moved_since_last_draw = true;
}

void MapView::on_uid_fix_fail()
{
  emit uid_fix_failed();

  _uid_fix_failed = true;
  deleteLater();
}

void MapView::initializeGL()
{
  using clock_t = std::chrono::steady_clock;
  auto const t0 = clock_t::now();
  LogError << "MapView::initializeGL: begin" << std::endl;

  bool uid_warning = false;

  OpenGL::context::scoped_setter const _ (::gl, context());

  gl.viewport(0.0f, 0.0f, width(), height());

  gl.clearColor (0.0f, 0.0f, 0.0f, 1.0f);

  if (_uid_fix == uid_fix_mode::max_uid)
  {
    _world->mapIndex.searchMaxUID();
    LogError << "MapView::initializeGL: after searchMaxUID (ms="
             << std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - t0).count()
             << ")" << std::endl;
  }
  else if (_uid_fix == uid_fix_mode::fix_all_fail_on_model_loading_error)
  {
    auto result = _world->mapIndex.fixUIDs (_world.get(), true);

    if (result == uid_fix_status::failed)
    {
      on_uid_fix_fail();
      return;
    }
    LogError << "MapView::initializeGL: after fixUIDs(fail_on_error) (ms="
             << std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - t0).count()
             << ")" << std::endl;
  }
  else if (_uid_fix == uid_fix_mode::fix_all_fuckporting_edition)
  {
    auto result = _world->mapIndex.fixUIDs (_world.get(), false);

    uid_warning = result == uid_fix_status::done_with_errors;
    LogError << "MapView::initializeGL: after fixUIDs(fuckporting) (ms="
             << std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - t0).count()
             << ")" << std::endl;
  }

  _uid_fix = uid_fix_mode::none;

  if (!_from_bookmark)
  {
    move_camera_with_auto_height (_camera.position);
  }
  LogError << "MapView::initializeGL: after move_camera_with_auto_height (ms="
           << std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - t0).count()
           << ")" << std::endl;

  if (uid_warning)
  {
    QMessageBox::warning
      ( nullptr
      , "UID Warning"
      , "Some models were missing or couldn't be loaded. "
        "This will lead to culling (visibility) errors in game\n"
        "It is recommended to fix those models (listed in the log file) and run the uid fix all again."
      , QMessageBox::Ok
      );
  }

  _imgui_context = QtImGui::initialize(this);
  LogError << "MapView::initializeGL: after QtImGui::initialize (ms="
           << std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - t0).count()
           << ")" << std::endl;

  emit resized();

  _last_opengl_context = context();

  _world->renderer()->upload();
  LogError << "MapView::initializeGL: after renderer()->upload (ms="
           << std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - t0).count()
           << ")" << std::endl;
  onSettingsSave();

  _buffers.upload();
  LogError << "MapView::initializeGL: after _buffers.upload (ms="
           << std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - t0).count()
           << ")" << std::endl;

  gl.bufferData<GL_PIXEL_PACK_BUFFER>(_buffers[0], 4, nullptr, GL_DYNAMIC_READ);
  gl.bufferData<GL_PIXEL_PACK_BUFFER>(_buffers[1], 4, nullptr, GL_DYNAMIC_READ);

  connect(context(), &QOpenGLContext::aboutToBeDestroyed, [this](){ emit aboutToLooseContext(); });

  _gl_initialized = true;

  LogError << "MapView::initializeGL: end (ms="
           << std::chrono::duration_cast<std::chrono::milliseconds>(clock_t::now() - t0).count()
           << ")" << std::endl;
}

void MapView::paintGL()
{
  ZoneScoped;

  static bool lock = false;
  static bool logged_once = false;
  if (!logged_once)
  {
    logged_once = true;
    LogError << "MapView::paintGL: first entry" << std::endl;
  }

  if (lock)
    return;

  if (!_needs_redraw)
    return;
  else
    _needs_redraw = false;

  if (!_gl_initialized)
  {
    initializeGL();
  }

  if (_last_opengl_context != context())
  {
    _gl_initialized = false;
    return;
  }

  const qreal now(_startup_time.elapsed() / 1000.0);

  _last_frame_durations.emplace_back (now - _last_update);

  lock = true;
  if (!activeTool()->preRender())
  {
      lock = false;
      return;
  }
  lock = false;

  OpenGL::context::scoped_setter const _(::gl, context());
  makeCurrent();

  Noggit::register_crash_render_stage("MapView::paintGL:after_makeCurrent");
  gl.clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  {
    lock = true;
    try
    {
      Noggit::register_crash_render_stage("MapView::paintGL:draw_map");
      LogError << "MapView::paintGL: before draw_map" << std::endl;
      draw_map();
      Noggit::register_crash_render_stage("MapView::paintGL:postRender");
      LogError << "MapView::paintGL: after draw_map" << std::endl;
      activeTool()->postRender();
    }
    catch (std::exception const& e)
    {
      LogError << "MapView::paintGL: caught std::exception during draw_map/postRender: " << e.what() << std::endl;
      try
      {
        std::cerr.flush();
      }
      catch (...)
      {
      }
    }
    catch (...)
    {
      LogError << "MapView::paintGL: caught non-std exception during draw_map/postRender" << std::endl;
      try
      {
        std::cerr.flush();
      }
      catch (...)
      {
      }
    }
    lock = false;
    tick (now - _last_update);
  }

  _last_update = now;

  if (_gizmo_on.get() && _world->has_selection() && !_world->selectedPointLightIndex().has_value())
  {
    ImGui::SetCurrentContext(_imgui_context);
    QtImGui::newFrame();

    static bool is_open = false;
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::SetNextWindowPos(ImVec2(-100.f, -100.f));
    ImGui::Begin("Gizmo", &is_open, ImGuiWindowFlags_::ImGuiWindowFlags_NoTitleBar
                                                | ImGuiWindowFlags_::ImGuiWindowFlags_NoBackground);

    // auto mv = model_view();
    // auto proj = projection();

    _transform_gizmo.setCurrentGizmoOperation(_gizmo_operation);
    _transform_gizmo.setCurrentGizmoMode(_gizmo_mode);
    _transform_gizmo.setUseMultiselectionPivot(activeTool()->useMultiselectionPivot());

    auto pivot = _world->multi_select_pivot().has_value() ?
        _world->multi_select_pivot().value() : glm::vec3(0.f, 0.f, 0.f);

    _transform_gizmo.setMultiselectionPivot(pivot);

    _transform_gizmo.handleTransformGizmo(this, _world->current_selection(), _model_view, _projection);

    // _world->update_selection_pivot();
    activeTool()->renderImGui(_gizmo_mode, _gizmo_operation);

    ImGui::End();

    /* Example
    std::string sText;

    if(ImGui::IsMouseClicked( 1 ) )
    {
      ImGui::OpenPopup( "PieMenu" );
    }

    if( BeginPiePopup( "PieMenu", 1 ) )
    {
      if( PieMenuItem( "Test1" ) ) sText = "Test1";
      if( PieMenuItem( "Test2" ) )
      {
        sText = "Test2";
      }
      if( PieMenuItem( "Test3", false ) ) sText = "Test3";
      if( BeginPieMenu( "Sub" ) )
      {
        if( BeginPieMenu( "Sub sub\nmenu" ) )
        {
          if( PieMenuItem( "SubSub" ) ) sText = "SubSub";
          if( PieMenuItem( "SubSub2" ) ) sText = "SubSub2";
          EndPieMenu();
        }
        if( PieMenuItem( "TestSub" ) ) sText = "TestSub";
        if( PieMenuItem( "TestSub2" ) ) sText = "TestSub2";
        EndPieMenu();
      }
      if( BeginPieMenu( "Sub2" ) )
      {
        if( PieMenuItem( "TestSub" ) ) sText = "TestSub";
        if( BeginPieMenu( "Sub sub\nmenu" ) )
        {
          if( PieMenuItem( "SubSub" ) ) sText = "SubSub";
          if( PieMenuItem( "SubSub2" ) ) sText = "SubSub2";
          EndPieMenu();
        }
        if( PieMenuItem( "TestSub2" ) ) sText = "TestSub2";
        EndPieMenu();
      }

      EndPiePopup();
    }

   */

    //ImGui::ShowDemoWindow();
    //ImGui::ShowStyleEditor();

    ImGui::Render();

  }

  // Point light gizmo: only in the point light tool (world pick is gated there too).
  if (terrainMode == editing_mode::point_light && _gizmo_on.get() && _world->selectedPointLightIndex().has_value()
      && !_point_light_suppress_gizmo_for_color_pick
      && _display_mode == display_mode::in_3D)
  {
    flushPointLightPropertyUndoBatch();
    ImGui::SetCurrentContext(_imgui_context);
    QtImGui::newFrame();

    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::SetNextWindowPos(ImVec2(-100.f, -100.f));
    // No p_open: this host window is only a draw target; a bool would let ImGui dismiss it on some IO paths.
    ImGui::Begin("PointLightGizmo", nullptr, ImGuiWindowFlags_::ImGuiWindowFlags_NoTitleBar
                                                  | ImGuiWindowFlags_::ImGuiWindowFlags_NoBackground);

    ImGuizmo::SetID(static_cast<int>(Noggit::Ui::Tools::ViewportGizmo::GizmoContext::MAP_VIEW) + 1337);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetScaleGizmoAxisLock(true);
    ImGuizmo::BeginFrame();

    ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

    auto& lights = _world->pointLights();
    auto idx = *_world->selectedPointLightIndex();
    if (idx < lights.size())
    {
      auto& l = lights[idx];

      glm::mat4x4 const rot_m = glm::eulerAngleXYZ (l.rotation_radians.x
                                                    , l.rotation_radians.y
                                                    , l.rotation_radians.z);
      glm::mat4x4 object_matrix;
      if (l.light_type == World::MapLightType::Spot)
      {
        glm::vec3 const s = glm::max (l.spot_gizmo_scale, glm::vec3 (0.01f));
        object_matrix = glm::translate (glm::mat4x4 (1.f), l.position) * rot_m * glm::scale (glm::mat4x4 (1.f), s);
      }
      else
      {
        object_matrix = glm::translate (glm::mat4x4 (1.f), l.position) * rot_m;
      }
      glm::mat4x4 delta_matrix = glm::mat4x4(1.f);

      ImGuizmo::OPERATION op = _gizmo_operation;
      if (l.light_type == World::MapLightType::Point)
        op = ImGuizmo::OPERATION::TRANSLATE;

      ImGuizmo::Manipulate(glm::value_ptr(_model_view), glm::value_ptr(_projection),
                           op, _gizmo_mode,
                           glm::value_ptr(object_matrix), glm::value_ptr(delta_matrix), nullptr);

      // Used on the next LMB path before this frame's Manipulate runs again (see doSelection).
      _point_light_gizmo_was_over = ImGuizmo::IsOver();

      bool const using_gizmo = ImGuizmo::IsUsing();

      if (using_gizmo)
      {
        if (!_point_light_gizmo_edit_action && !NOGGIT_CUR_ACTION && !_point_light_numpad_edit_action
            && !_point_light_mmb_edit_action)
        {
          if (Noggit::Action* action = NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::ePOINT_LIGHTS_CHANGED,
                                                                    Noggit::ActionModalityControllers::eLMB))
          {
            action->registerPointLightsChange();
            _point_light_gizmo_edit_action = true;
          }
        }

        // Match ViewportGizmo::handleTransformGizmo: decompose delta_matrix only and apply increments.
        // Full-matrix decompose reintroduced unstable euler extraction (jitter / spasms while translating).
        glm::vec3 new_scale;
        glm::quat new_orientation;
        glm::vec3 new_translation;
        glm::vec3 new_skew_;
        glm::vec4 new_perspective_;
        glm::decompose(delta_matrix, new_scale, new_orientation, new_translation, new_skew_, new_perspective_);
        new_orientation = glm::conjugate(new_orientation);

        ImGuizmo::OPERATION const apply_op = (l.light_type == World::MapLightType::Point)
                                                 ? ImGuizmo::OPERATION::TRANSLATE
                                                 : op;

        bool changed = false;
        switch (apply_op)
        {
          case ImGuizmo::OPERATION::TRANSLATE:
            if (new_translation.x != 0.0f || new_translation.y != 0.0f || new_translation.z != 0.0f)
            {
              l.position += new_translation;
              l.tile_x = static_cast<std::uint16_t>(std::clamp(int(l.position.x / TILESIZE), 0, 63));
              l.tile_y = static_cast<std::uint16_t>(std::clamp(int(l.position.z / TILESIZE), 0, 63));
              changed = true;
            }
            break;
          case ImGuizmo::OPERATION::ROTATE:
            if (l.light_type == World::MapLightType::Spot)
            {
              // Same early-out and euler path as ViewportGizmo (single-selection branch).
              if (new_orientation.x != -0.0f || new_orientation.y != -0.0f || new_orientation.z != -0.0f)
              {
                glm::vec3 rot_euler = glm::eulerAngles(new_orientation);
                rot_euler *= -1.f;
                rot_euler *= 57.2957795f;
                l.rotation_radians.x += glm::radians(rot_euler.x);
                l.rotation_radians.y += glm::radians(rot_euler.y);
                l.rotation_radians.z += glm::radians(rot_euler.z);
                changed = true;
              }
            }
            break;
          case ImGuizmo::OPERATION::SCALE:
            if (l.light_type == World::MapLightType::Spot)
            {
              if (new_scale.x != 1.0f || new_scale.y != 1.0f || new_scale.z != 1.0f)
              {
                // Single-selection object gizmo assigns uniform scale from delta; spots keep per-axis.
                l.spot_gizmo_scale = glm::max(new_scale, glm::vec3(0.01f));
                changed = true;
              }
            }
            break;
          default:
            break;
        }

        if (changed)
          invalidate();
      }
      else if (_point_light_gizmo_edit_action)
      {
        if (NOGGIT_CUR_ACTION)
          NOGGIT_ACTION_MGR->endAction();
        _point_light_gizmo_edit_action = false;
      }
    }
    else
    {
      _point_light_gizmo_was_over = false;
    }

    ImGui::End();

    ImGui::Render();
  }
  else
  {
    _point_light_gizmo_was_over = false;
  }

  if (_world->uid_duplicates_found() && !_uid_duplicate_warning_shown)
  {
    _uid_duplicate_warning_shown = true;

    QMessageBox::critical( this
        , "UID ALREADY IN USE"
        , "Please enable 'Always check for max UID', mysql uid store or synchronize your "
          "uid.ini file if you're sharing the map between several mappers.\n\n"
          "Use 'Editor > Force uid check on next opening' to fix the issue."
    );
  }

  FrameMark
}

void MapView::resizeGL (int width, int height)
{
  OpenGL::context::scoped_setter const _ (::gl, context());
  gl.viewport(0.0f, 0.0f, width, height);
  emit resized();
  _camera_moved_since_last_draw = true;
  _needs_redraw = true;
}


MapView::~MapView()
{
  Noggit::Integrations::DiscordRichPresence::instance().shutdown();

  makeCurrent();

  _destroying = true;

  if (QCoreApplication* app = QCoreApplication::instance())
    app->removeEventFilter(this);
  for (auto const& ptr : _point_light_color_pickers)
    if (ptr)
      ptr->removeEventFilter(this);
  _point_light_color_pickers.clear();
  _point_light_active_color_dialog.clear();

  _main_window->removeToolBar(_main_window->_app_toolbar);

  OpenGL::context::scoped_setter const _ (::gl, context());
  delete _texBrush;
  delete _viewport_overlay_ui;

  // when the uid fix fail the UI isn't created
  if (!_uid_fix_failed)
  {
    // delete TexturePicker; // explicitly delete this here to avoid opengl context related crash
    // delete objectEditor;
    // since the ground effect tool preview renderer got added, this causes crashing on exit to menu. 
    // Now it crashes in application exit.
    // delete texturingTool;

    _tools[static_cast<int>(editing_mode::paint)].reset();
    _tools[static_cast<int>(editing_mode::object)].reset();
  }
  
  if (_force_uid_check)
  {
    uid_storage::remove_uid_for_map(_world->getMapID());
  }

  _world.reset();

  AsyncLoader::instance->reset_object_fail();

  Noggit::Ui::selected_texture::texture.reset();

  ModelManager::report();
  TextureManager::report();
  WMOManager::report();

  flushPointLightPropertyUndoBatch();
  NOGGIT_ACTION_MGR->disconnect();

  _buffers.unload();

}

void MapView::tick (float dt)
{
	_mod_shift_down = QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
	_mod_ctrl_down = QApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
	_mod_alt_down = QApplication::keyboardModifiers().testFlag(Qt::AltModifier);
	_mod_num_down = QApplication::keyboardModifiers().testFlag(Qt::KeypadModifier);

  if (_discord_rich_presence_enabled && !_discord_rich_presence_app_id.empty())
  {
    if (!_discord_rich_presence_start_ts.has_value())
      _discord_rich_presence_start_ts = static_cast<std::int64_t>(QDateTime::currentSecsSinceEpoch());

    std::string const map_name = _world ? _world->basename : std::string{};
    std::string const tool_name = activeTool() ? std::string(activeTool()->name()) : std::string{};

    if (map_name != _discord_last_map || tool_name != _discord_last_tool)
    {
      _discord_last_map = map_name;
      _discord_last_tool = tool_name;

      Noggit::Integrations::DiscordActivity activity;
      // Match UI mockup:
      // Line 2 (details): "In Map- <MapName>"
      // Line 3 (state):   "In Tool- <ToolName>"
      activity.details = map_name.empty() ? "In Map- (none)" : ("In Map- " + map_name);
      activity.state = tool_name.empty() ? "In Tool- (none)" : ("In Tool- " + tool_name);
      activity.tool.clear();
      activity.start_timestamp_unix = *_discord_rich_presence_start_ts;
      Noggit::Integrations::DiscordRichPresence::instance().setActivity(std::move(activity));
    }
  }

	unsigned action_modality = 0;
	if (_mod_shift_down)
    action_modality |= Noggit::ActionModalityControllers::eSHIFT;
	if (_mod_ctrl_down)
    action_modality |= Noggit::ActionModalityControllers::eCTRL;
  if (_mod_alt_down)
    action_modality |= Noggit::ActionModalityControllers::eALT;
  if (_mod_num_down)
    action_modality |= Noggit::ActionModalityControllers::eNUM;
  if (_mod_space_down)
    action_modality |= Noggit::ActionModalityControllers::eSPACE;
  if (leftMouse)
    action_modality |= Noggit::ActionModalityControllers::eLMB;
  if (rightMouse)
    action_modality |= Noggit::ActionModalityControllers::eRMB;
  if (middleMouse)
    action_modality |= Noggit::ActionModalityControllers::eMMB;

  action_modality |= activeTool()->actionModality();
  // if (keyx != 0 || keyy != 0 || keyz != 0)
  //   action_modality |= Noggit::ActionModalityControllers::eTRANSLATE;

  NOGGIT_ACTION_MGR->endActionOnModalityMismatch(action_modality);

  // Numpad movement for selected point lights (same keys/modifiers as object selection move), point light tool only.
  if (_mod_num_down && terrainMode == editing_mode::point_light)
  {
    flushPointLightPropertyUndoBatch();
    auto idx_opt = _world->selectedPointLightIndex();
    if (idx_opt.has_value())
    {
      auto& lights = _world->pointLights();
      auto idx = *idx_opt;
      if (idx < lights.size())
      {
        bool const moving_keys = (_point_light_keyx != 0.0f || _point_light_keyz != 0.0f);
        if (moving_keys && !NOGGIT_CUR_ACTION && !_point_light_gizmo_edit_action && !_point_light_mmb_edit_action)
        {
          if (Noggit::Action* action = NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::ePOINT_LIGHTS_CHANGED,
                                                                      Noggit::ActionModalityControllers::eNUM))
          {
            action->registerPointLightsChange();
            _point_light_numpad_edit_action = true;
          }
        }

        if (moving_keys)
        {
          float numpad_moveratio = 0.001f;
          if (_mod_ctrl_down && _mod_shift_down)
            numpad_moveratio += 0.5f;
          else if (_mod_shift_down)
            numpad_moveratio += 0.05f;
          else if (_mod_ctrl_down)
            numpad_moveratio += 0.005f;

          auto& l = lights[idx];
          glm::vec3 const try_pos{ l.position.x + _point_light_keyx * numpad_moveratio
                                  , l.position.y
                                  , l.position.z + _point_light_keyz * numpad_moveratio };
          l.position = try_pos;
          l.tile_x = static_cast<std::uint16_t>(std::clamp(int(l.position.x / TILESIZE), 0, 63));
          l.tile_y = static_cast<std::uint16_t>(std::clamp(int(l.position.z / TILESIZE), 0, 63));
          invalidate();
        }
      }
    }
  }
  else
  {
    _point_light_numpad_edit_action = false;
  }

  // start unloading tiles
  _world->mapIndex.enterTile (TileIndex (_camera.position));
  if (_unload_tiles)
    _world->mapIndex.unloadTiles (TileIndex (_camera.position));

  dt = std::min(dt, 1.0f);

  math::degrees yaw (-_camera.yaw()._);

  glm::vec3 dir(1.0f, 0.0f, 0.0f);
  glm::vec3 dirUp(1.0f, 0.0f, 0.0f);
  glm::vec3 dirRight(0.0f, 0.0f, 1.0f);
  math::rotate(0.0f, 0.0f, &dir.x, &dir.y, _camera.pitch());
  math::rotate(0.0f, 0.0f, &dir.x, &dir.z, yaw);

  if (_mod_ctrl_down)
  {
    dirUp.x = 0.0f;
    dirUp.y = 1.0f;
    math::rotate(0.0f, 0.0f, &dirUp.x, &dirUp.y, _camera.pitch());
    math::rotate(0.0f, 0.0f, &dirRight.x, &dirRight.y, _camera.pitch());
    math::rotate(0.0f, 0.0f, &dirUp.x, &dirUp.z, yaw);
    math::rotate(0.0f, 0.0f, &dirRight.x, &dirRight.z,yaw);
  }
  else if(!_mod_shift_down)
  {
    math::rotate(0.0f, 0.0f, &dirUp.x, &dirUp.z, yaw);
    math::rotate(0.0f, 0.0f, &dirRight.x, &dirRight.z, yaw);
  }

  // note : selection update most commonly happens in mouseReleaseEvent, which sets leftMouse to false
  bool selection_changed = false;

  // update camera
  if (_display_mode == display_mode::in_3D)
  {
    if (turn)
    {
      _camera.add_to_yaw(math::degrees(turn));
      _camera_moved_since_last_draw = true;
    }
    if (lookat)
    {
      _camera.add_to_pitch(math::degrees(lookat));
      _camera_moved_since_last_draw = true;
    }

    if (moving)
    {
      _camera.move_forward(moving, dt);
      _camera_moved_since_last_draw = true;
    }
    if (strafing)
    {
      _camera.move_horizontal(strafing, dt);
      _camera_moved_since_last_draw = true;
    }
    if (updown)
    {
      _camera.move_vertical(updown, dt);
      _camera_moved_since_last_draw = true;
    }

    if (_camera_moved_since_last_draw)
    {
      if (_fps_mode.get())
      {
        // there is a also hack to update camera when entering mode in void ViewToolbar::add_tool_icon()
        float h = _world->get_ground_height(_camera.position).y;
        _camera.position.y = h + 3.f;
      }
      else if (_camera_collision.get())
      {
        float h = _world.get()->get_ground_height(_camera.position).y;
        if (_camera.position.y < h + 3.f)
        {
          _camera.position.y = h + 3.f;
        }
      }
    }
  }
  else if (_display_mode == display_mode::in_2D)
  {
    //! \todo this is total bullshit. there should be a seperate view and camera class for tilemode
    if (moving)
    {
      _camera.position.z -= dt * _camera.move_speed * moving;
      _camera_moved_since_last_draw = true;
    }
    if (strafing)
    {
      _camera.position.x += dt * _camera.move_speed * strafing;
      _camera_moved_since_last_draw = true;
    }
    if (updown)
    {
      _2d_zoom *= pow(2.0f, dt * updown * 4.0f);
      _2d_zoom = std::max(0.01f, _2d_zoom);
      _camera_moved_since_last_draw = true;
    }
  }

  // udpate MVP after moving camera
  _model_view = model_view(_debug_cam_mode.get());
  _projection = projection();

  // update cursor pos after camera
  auto cur_action = NOGGIT_CUR_ACTION;

  if ((cur_action && !cur_action->getBlockCursor()) || !cur_action)
  {
    if (_locked_cursor_mode.get())
    {
      switch (terrainMode)
      {
      case editing_mode::areaid:
      case editing_mode::impass:
      case editing_mode::holes:
      case editing_mode::object:
        update_cursor_pos();
        break;
      default:
        break;
      }
    }
    else
    {
      update_cursor_pos();
    }
  }

  // Point light MMB: snap to terrain/object under cursor (like ObjectTool + move model to cursor), point light tool only.
  // Shift+MMB: keep XZ, set Y from the hit under the cursor (vertical follow).
  if (terrainMode == editing_mode::point_light && _display_mode == display_mode::in_3D && middleMouse && !_locked_cursor_mode.get()
      && !_point_light_suppress_gizmo_for_color_pick && _world->selectedPointLightIndex().has_value())
  {
    auto idx_opt = _world->selectedPointLightIndex();
    auto& lights = _world->pointLights();
    if (idx_opt && *idx_opt < lights.size())
    {
      flushPointLightPropertyUndoBatch();
      if (!_point_light_mmb_edit_action && !NOGGIT_CUR_ACTION && !_point_light_gizmo_edit_action
          && !_point_light_numpad_edit_action)
      {
        if (Noggit::Action* action = NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::ePOINT_LIGHTS_CHANGED,
                                                                    Noggit::ActionModalityControllers::eMMB))
        {
          action->registerPointLightsChange();
          _point_light_mmb_edit_action = true;
        }
      }

      auto& l = lights[*idx_opt];
      glm::vec3 const try_pos = _mod_shift_down ? glm::vec3(l.position.x, _cursor_pos.y, l.position.z) : _cursor_pos;

      l.position = try_pos;
      l.tile_x = static_cast<std::uint16_t>(std::clamp(int(l.position.x / TILESIZE), 0, 63));
      l.tile_y = static_cast<std::uint16_t>(std::clamp(int(l.position.z / TILESIZE), 0, 63));
      invalidate();
    }
  }

  // _minimap->update(); // causes massive performance issues, should only be done when moving
  Noggit::TickParameters tickParams
  {
      .displayMode = _display_mode,
      .underMap = _world->isUnderMap(_cursor_pos),
      .camera_moved_since_last_draw = _camera_moved_since_last_draw,
      .left_mouse = leftMouse,
      .right_mouse = rightMouse,
      .mod_shift_down = _mod_shift_down,
      .mod_ctrl_down = _mod_ctrl_down,
      .mod_alt_down = _mod_alt_down,
      .mod_num_down = _mod_num_down,
      .dir = dir,
      .dirUp = dirUp,
      .dirRight = dirRight,
  };

  float effective_dt = dt;
  if (_tablet_pressure_strength && _tablet_manager && _tablet_manager->isActive())
  {
    float const p = static_cast<float>(std::clamp(_tablet_manager->pressure(), 0.0, 1.0));
    effective_dt *= p;
  }

  activeTool()->onTick(effective_dt, tickParams);

  auto currentSelection = _world->current_selection();
  if (_world->has_selection())
  {
    // update rotation editor if the selection has changed
    if (lastSelected != currentSelection)
    {
      selection_changed = true;
      emit rotationChanged();
    }
  }

  _world->time += this->mTimespeed * dt;
  _world->animtime += dt * 1000.0f;

  if (_draw_model_animations.get())
  {
    _world->update_models_emitters(dt);
  }

  if (_world->has_selection())
  {
    lastSelected = currentSelection;
  }

  QString status;
  status += ( QString ("tile: %1 %2")
            . arg (std::floor (_camera.position.x / TILESIZE))
            . arg (std::floor (_camera.position.z / TILESIZE))
            );
  status += ( QString ("; coordinates client: (%1, %2, %3), server: (%4, %5, %6)")
            . arg (_camera.position.x, 0, 'f', 2)
            . arg (_camera.position.z, 0, 'f', 2)
            . arg (_camera.position.y, 0, 'f', 2)
            . arg (ZEROPOINT - _camera.position.z, 0, 'f', 2)
            . arg (ZEROPOINT - _camera.position.x, 0, 'f', 2)
            . arg (_camera.position.y, 0, 'f', 2)
            );

  _status_position->setText (status);

  if (currentSelection.size() > 0) // currently disabled, change to == to enable status bar selection
  {
    _status_selection->setText ("");
  }
  else if (currentSelection.size() == 1)
  {
    switch (currentSelection.begin()->index())
    {
    case eEntry_Object:
      {
        auto obj = std::get<selected_object_type>(*currentSelection.begin());

        if (obj->which() == eMODEL)
        {
          auto instance(static_cast<ModelInstance*>(obj));
          _status_selection->setText
              ( QString ("%1: %2")
                    . arg (instance->uid)
                    . arg (QString::fromStdString (instance->model->file_key().stringRepr()))
              );
        }
        else if (obj->which() == eWMO)
        {
          auto instance(static_cast<WMOInstance*>(obj));
          _status_selection->setText
              ( QString ("%1: %2")
                    . arg (instance->uid)
                    . arg (QString::fromStdString (instance->wmo->file_key().stringRepr()))
              );
        }

        break;
      }
    case eEntry_MapChunk:
      {
      auto chunk(std::get<selected_chunk_type>(*currentSelection.begin()).chunk);
        _status_selection->setText
          (QString ("%1, %2").arg (chunk->px).arg (chunk->py));
        break;
      }
    }
  }
  else
  {
	  _status_selection->setText(QString::number(currentSelection.size()) + " objects selected");
  }

  if (selection_changed || NOGGIT_CUR_ACTION)
    updateDetailInfos(); // checks if sel changed

  if (selection_changed)
  {
      emit selectionUpdated(currentSelection);
      // updateDetailInfos();
  }

  _status_area->setText
    (QString::fromStdString (gAreaDB.getAreaFullName (_world->getAreaID (_camera.position))));

  {
    int time ((static_cast<int>(_world->time) % 2880) / 2);
    std::stringstream timestrs;
    timestrs << "Time: " << (time / 60) << ":" << std::setfill ('0')
             << std::setw (2) << (time % 60);


    timestrs << ", Pres: " << _tablet_manager->pressure();

    _status_time->setText (QString::fromStdString (timestrs.str()));
  }

  _last_fps_update += dt;

  // update fps every sec
  if (_last_fps_update > 1.f && !_last_frame_durations.empty())
  {
    auto avg_frame_duration
      ( std::accumulate ( _last_frame_durations.begin()
                        , _last_frame_durations.end()
                        , 0.
                        )
      / qreal (_last_frame_durations.size())
      );
    _status_fps->setText ( "FPS: " + QString::number (int (1. / avg_frame_duration)) 
                         + " - Average frame time: " + QString::number(avg_frame_duration*1000.0) + "ms"
                         );

    _last_frame_durations.clear();
    _last_fps_update = 0.f;
  }

  _status_culling->setText ( "Loaded tiles: " + QString::number(_world->getNumLoadedTiles())
                         + ", Rendered tiles: " + QString::number(_world->getNumRenderedTiles())
                         + "\t Loaded objects: " + QString::number(_world->getModelInstanceStorage().getTotalModelsCount())
                         + ", Rendered objects: " + QString::number(_world->getNumRenderedObjects())
  );
}

glm::vec4 MapView::normalized_device_coords (int x, int y) const
{
  return {2.0f * x / width() - 1.0f, 1.0f - 2.0f * y / height(), 0.0f, 1.0f};
}

float MapView::aspect_ratio() const
{
  return float (width()) / float (height());
}

math::ray MapView::intersect_ray() const
{
  float mx = _last_mouse_pos.x(), mz = _last_mouse_pos.y();

  if (_display_mode == display_mode::in_3D)
  {
    // during rendering we multiply perspective * view
    // so we need the same order here and then invert.
    glm::mat4x4 const invertedViewMatrix = glm::inverse(_projection * _model_view);
    auto normalisedView = invertedViewMatrix * normalized_device_coords(mx, mz);

    auto pos = glm::vec3(normalisedView.x / normalisedView.w, normalisedView.y / normalisedView.w, normalisedView.z / normalisedView.w);

    return { _camera.position, pos - _camera.position };
  }
  else
  {
    glm::vec3 const pos
    ( _camera.position.x - (width() * 0.5f - mx) * _2d_zoom
    , _camera.position.y
    , _camera.position.z - (height() * 0.5f - mz) * _2d_zoom
    );
    
    return { pos, glm::vec3(0.f, -1.f, 0.f) };
  }
}

selection_result MapView::intersect_result(bool terrain_only)
{
  selection_result results
  ( _world->intersect 
    ( glm::transpose(_model_view)
    , intersect_ray()
    , terrain_only
    , terrainMode == editing_mode::object || terrainMode == editing_mode::minimap
    , _draw_terrain.get()
    , _draw_wmo.get()
    , _draw_models.get()
    , _draw_hidden_models.get()
    , _draw_wmo_exterior.get()
    , _draw_model_animations.get()
    )
  );

  std::sort ( results.begin()
            , results.end()
            , [](selection_entry const& lhs, selection_entry const& rhs)
              {
                return lhs.first < rhs.first;
              }
            );

  return std::move(results);
}

math::ray MapView::intersect_ray_from_pixel(QPointF const& mouse_px) const
{
  float mx = mouse_px.x(), mz = mouse_px.y();

  if (_display_mode == display_mode::in_3D)
  {
    glm::mat4x4 const invertedViewMatrix = glm::inverse(_projection * _model_view);
    auto normalisedView = invertedViewMatrix * normalized_device_coords(static_cast<int>(mx), static_cast<int>(mz));

    auto pos = glm::vec3(normalisedView.x / normalisedView.w, normalisedView.y / normalisedView.w, normalisedView.z / normalisedView.w);

    return { _camera.position, pos - _camera.position };
  }
  else
  {
    glm::vec3 const pos
    ( _camera.position.x - (width() * 0.5f - mx) * _2d_zoom
    , _camera.position.y
    , _camera.position.z - (height() * 0.5f - mz) * _2d_zoom
    );

    return { pos, glm::vec3(0.f, -1.f, 0.f) };
  }
}

selection_result MapView::intersect_result(bool terrain_only, QPoint const& mouse_px) const
{
  selection_result results
  ( _world->intersect
    ( glm::transpose(_model_view)
    , intersect_ray_from_pixel(mouse_px)
    , terrain_only
    , terrainMode == editing_mode::object || terrainMode == editing_mode::minimap
    , _draw_terrain.get()
    , _draw_wmo.get()
    , _draw_models.get()
    , _draw_hidden_models.get()
    , _draw_wmo_exterior.get()
    , _draw_model_animations.get()
    )
  );

  std::sort ( results.begin()
            , results.end()
            , [](selection_entry const& lhs, selection_entry const& rhs)
              {
                return lhs.first < rhs.first;
              }
            );

  return results;
}

bool MapView::screenNearSelectedPointLight(QPointF mouse_px) const
{
  auto const sel = _world->selectedPointLightIndex();
  if (!sel || *sel >= _world->pointLights().size())
    return false;

  auto const& l = _world->pointLights()[*sel];
  glm::vec4 const clip = _projection * _model_view * glm::vec4(l.position, 1.0f);
  if (clip.w <= 0.0f)
    return false;

  glm::vec3 const ndc = glm::vec3(clip) / clip.w;
  if (ndc.x < -1.2f || ndc.x > 1.2f || ndc.y < -1.2f || ndc.y > 1.2f)
    return false;

  float const sx = (ndc.x * 0.5f + 0.5f) * static_cast<float>(width());
  float const sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(height());

  float const dx = sx - static_cast<float>(mouse_px.x());
  float const dy = sy - static_cast<float>(mouse_px.y());
  float const dist_px = std::sqrt(dx * dx + dy * dy);

  // ImGuizmo translation arrows extend well beyond the light's screen-space pick disc.
  static constexpr float k_slop_px = 120.f;
  return dist_px <= k_slop_px;
}

void MapView::doSelection (bool selectTerrainOnly, bool mouseMove)
{
  if (_world->get_selected_model_count() && _gizmo_on.get() && (_transform_gizmo.isUsing() || _transform_gizmo.isOver()))
    return;

  // Point lights are only selectable in the point light tool (see set_editing_mode for clear on mode change).
  if (terrainMode == editing_mode::point_light && !mouseMove && _display_mode == display_mode::in_3D
      && !_transform_gizmo.isUsing() && !ImGuizmo::IsUsing())
  {
    auto& lights = _world->pointLights();

    // Screen-space pick: project each light to pixels and pick the closest one under mouse.
    float best_px = std::numeric_limits<float>::max();
    float best_depth = std::numeric_limits<float>::max();
    std::optional<std::size_t> best_idx = std::nullopt;

    for (std::size_t i = 0; i < lights.size(); ++i)
    {
      auto const& l = lights[i];

      glm::vec4 const clip = _projection * _model_view * glm::vec4(l.position, 1.0f);
      if (clip.w <= 0.0f)
        continue;

      glm::vec3 const ndc = glm::vec3(clip) / clip.w;
      if (ndc.x < -1.2f || ndc.x > 1.2f || ndc.y < -1.2f || ndc.y > 1.2f)
        continue;

      float const sx = (ndc.x * 0.5f + 0.5f) * static_cast<float>(width());
      float const sy = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(height());

      float const dx = sx - static_cast<float>(_last_mouse_pos.x());
      float const dy = sy - static_cast<float>(_last_mouse_pos.y());
      float const dist_px = std::sqrt(dx * dx + dy * dy);

      float const pick_px = std::clamp(l.attenuation_end * 0.6f, 10.0f, 30.0f);
      if (dist_px <= pick_px)
      {
        if (dist_px < best_px || (dist_px == best_px && ndc.z < best_depth))
        {
          best_px = dist_px;
          best_depth = ndc.z;
          best_idx = i;
        }
      }
    }

    if (best_idx.has_value())
    {
      _world->selectedPointLightIndex(best_idx);
      setPointLightPropertyEditFallback(*best_idx);
      _world->reset_selection();
      invalidate();
      return;
    }

    // Click missed every light's pick radius (arrows are off-center), but may still be on the gizmo.
    // Skip world raycast for this click so terrain/objects behind the gizmo do not clear the selection.
    // Shift/Ctrl still run full picking so additive/chunk-range selection is unchanged.
    bool const mod_multiselect = _mod_shift_down || _mod_ctrl_down;
    if (!mod_multiselect
        && _gizmo_on.get()
        && _world->selectedPointLightIndex().has_value()
        && !_point_light_suppress_gizmo_for_color_pick
        && (_point_light_gizmo_was_over || screenNearSelectedPointLight(_last_mouse_pos)))
    {
      return;
    }
  }

  selection_result results(intersect_result(selectTerrainOnly));

  if (results.empty())
  {
    if (!mouseMove)
    {
      _world->reset_selection();
      _world->selectedPointLightIndex(std::nullopt);
      clearPointLightPropertyEditFallback();
    }
  }
  else
  {
    auto const& hit (results.front().second);
    if (!mouseMove)
    {
      _world->selectedPointLightIndex(std::nullopt);
      clearPointLightPropertyEditFallback();
    }

    if (terrainMode == editing_mode::object || terrainMode == editing_mode::minimap)
    {
      float radius = activeTool()->brushRadius();

      if (_mod_shift_down)
      {
        if (hit.index() == eEntry_Object)
        {
          if (!_world->is_selected(hit))
          {
            _world->add_to_selection(hit);
          }
          else if (!mouseMove)
          {
            _world->remove_from_selection(hit);
          }
        }
        else if (hit.index() == eEntry_MapChunk)
        {
          _world->range_add_to_selection(_cursor_pos, radius, false);
        }
      }
      else if (_mod_ctrl_down)
      {
        if (hit.index() == eEntry_MapChunk)
        {
          _world->range_add_to_selection(_cursor_pos, radius, true);
        }
      }
      else if (!_mod_space_down && !_mod_alt_down && !_mod_ctrl_down && !mouseMove)
      {
        // objectEditor->update_selection(_world.get());
        _world->reset_selection();
        _world->add_to_selection(hit);
      }
    }
    else if (hit.index() == eEntry_MapChunk && !mouseMove)
    {
      _world->reset_selection();
      _world->add_to_selection(hit);
    }

    auto action = NOGGIT_CUR_ACTION;

    if (!action || (!action->getBlockCursor()) || !_locked_cursor_mode.get())
    {
      _cursor_pos = hit.index() == eEntry_Object ? std::get<selected_object_type>(hit)->pos
                                                 : hit.index() == eEntry_MapChunk ? std::get<selected_chunk_type>(hit).position
                                                                                  : throw std::logic_error("bad variant");
    }

  }

  emit rotationChanged();
}

void MapView::update_cursor_pos()
{
  static bool buffer_switch = false;

  if (false && terrainMode != editing_mode::holes) // figure out why this does not work on every hardware.
  {
    float mx = _last_mouse_pos.x(), mz = _last_mouse_pos.y();

    //gl.readBuffer(GL_FRONT);
    gl.bindBuffer(GL_PIXEL_PACK_BUFFER, _buffers[static_cast<unsigned>(buffer_switch)]);

    gl.readPixels(mx, height() - mz - 1, 1, 1, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, 0);

    gl.bindBuffer(GL_PIXEL_PACK_BUFFER, _buffers[static_cast<unsigned>(!buffer_switch)]);
    GLushort* ptr = static_cast<GLushort*>(gl.mapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));

    buffer_switch = !buffer_switch;

    if(ptr)
    {
      glm::vec4 viewport = glm::vec4(0, 0, width(), height());
      glm::vec3 wincoord = glm::vec3(mx, height() - mz - 1, static_cast<float>(*ptr) / std::numeric_limits<unsigned short>::max());

      // glm::mat4x4 model_view_ = model_view();
      // glm::mat4x4 projection_ = projection();

      glm::vec3 objcoord = glm::unProject(wincoord, _model_view, _projection, viewport);


      TileIndex tile({objcoord.x, objcoord.y, objcoord.z});

      if (!_world->mapIndex.tileLoaded(tile))
      {
        gl.unmapBuffer(GL_PIXEL_PACK_BUFFER);
        gl.bindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        return;
      }

      _cursor_pos = {objcoord.x, objcoord.y, objcoord.z};

      gl.unmapBuffer(GL_PIXEL_PACK_BUFFER);
    }

    gl.bindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    return;
  }

  // use raycasting for holes

  selection_result results (intersect_result (true));

  if (!results.empty())
  {
    auto const& hit(results.front().second);
    // hit cannot be something else than a chunk
    auto const& chunkHit = std::get<selected_chunk_type>(hit);
    _cursor_pos = chunkHit.position;

  }
}

glm::mat4x4 MapView::model_view(bool use_debug_cam) const
{
  if (_display_mode == display_mode::in_2D)
  {
    glm::vec3 eye = use_debug_cam ? _debug_cam.position : _camera.position;
    glm::vec3 target = eye;
    target.y -= 1.f;
    target.z -= 0.001f;
    auto center = target;
    auto up = glm::vec3(0.f, 1.f, 0.f);

    return glm::lookAt(eye, target, up);
  }
  else
  {
    if (use_debug_cam)
    {
        return _debug_cam.look_at_matrix();
    }
    else
    {
        return _camera.look_at_matrix();
    }
  }
}

glm::mat4x4 MapView::projection() const
{
  // float far_z = _settings->value("view_distance", 2000.f).toFloat() + 1.f; // don't access qsettings in mainloop, it's slow
  float far_z = _world->renderer()->_view_distance - TILE_RADIUS + 1.0f;

  if (_display_mode == display_mode::in_2D)
  {
    float half_width = width() * 0.5f * _2d_zoom;
    float half_height = height() * 0.5f * _2d_zoom;

    return glm::ortho(-half_width, half_width, -half_height, half_height, -1.f, far_z);
  }
  else
  {
    return glm::perspective(_camera.fov()._, aspect_ratio(), _fps_mode.get() ? 0.1f : 1.f, far_z);
  }
}

void MapView::draw_map()
{
  ZoneScoped;

  Noggit::register_crash_render_stage("MapView::draw_map:enter");
  //! \ todo: make the current tool return the radius
  float radius = 0.0f, inner_radius = 0.0f, angle = 0.0f, orientation = 0.0f;
  glm::vec3 ref_pos;
  bool angled_mode = false, use_ref_pos = false;

  _cursorType = CursorType::CIRCLE;

  eTerrainType terrainType = eTerrainType_Flat;
  bool show_unpaintable_chunks = false;
  int displayed_water_layer = -1;
  auto cursorColor = cursor_color;
  MinimapRenderSettings minimapRenderSettings;

  auto draw_parameters = activeTool()->drawParameters();
  radius = draw_parameters.radius;
  inner_radius = draw_parameters.inner_radius;
  _cursorType = draw_parameters.cursor_type;
  terrainType = draw_parameters.terrain_type;
  angle = draw_parameters.angle;
  orientation = draw_parameters.orientation;
  ref_pos = draw_parameters.ref_pos;
  angled_mode = draw_parameters.angled_mode;
  use_ref_pos = draw_parameters.use_ref_pos;
  show_unpaintable_chunks = draw_parameters.show_unpaintable_chunks;
  displayed_water_layer = draw_parameters.displayed_water_layer;
  cursorColor = draw_parameters.cursor_color;
  minimapRenderSettings = draw_parameters.minimapRenderSettings;

  bool debug_cam = _debug_cam_mode.get();

  // math::frustum frustum(model_view(debug_cam) * projection());
  _model_view = model_view(debug_cam);
  _projection = projection();

  //! \note Select terrain below mouse, if no item selected or the item is map.
  if (!(_world->has_selection()
    || _locked_cursor_mode.get()))
  {
    // When a point light is selected, world selection is often still empty; treat per-frame picking as hover-only
    // (mouseMove=true) so empty ray hits do not clear the light every frame. World point-light picking itself is
    // only active in editing_mode::point_light (see doSelection).
    bool const hover_only = _world->selectedPointLightIndex().has_value();
    doSelection(true, hover_only);
  }

  if (_camera_moved_since_last_draw)
  {
      _minimap->update();
  }

  bool show_unpaintable = _classic_ui ? show_unpaintable_chunks : _left_sec_toolbar->showUnpaintableChunk();



  WorldRenderParams renderParams;

  renderParams.cursorRotation = _cursorRotation;
  renderParams.cursor_type = _cursorType;
  renderParams.brush_radius = radius;
  renderParams.show_unpaintable_chunks = show_unpaintable;
  renderParams.draw_only_inside_light_sphere = _left_sec_toolbar->drawOnlyInsideSphereLight();
  renderParams.draw_wireframe_light_sphere = _left_sec_toolbar->drawWireframeSphereLight();
  renderParams.alpha_light_sphere = _left_sec_toolbar->getAlphaSphereLight();
  renderParams.draw_point_lights = _draw_point_lights.get();
  renderParams.draw_point_light_spheres = _draw_point_light_spheres.get();
  renderParams.point_light_sphere_opacity = _point_light_sphere_opacity;
  renderParams.inner_radius_ratio = inner_radius;
  renderParams.angle = angle;
  renderParams.orientation = orientation;
  renderParams.use_ref_pos = use_ref_pos;
  renderParams.angled_mode = angled_mode;
  renderParams.draw_paintability_overlay = terrainMode == editing_mode::paint;
  renderParams.editing_mode = terrainMode;
  renderParams.camera_moved = debug_cam ? false : _camera_moved_since_last_draw;
  renderParams.draw_mfbo = _draw_mfbo.get();
  renderParams.draw_terrain = _draw_terrain.get();
  renderParams.draw_wmo = _draw_wmo.get();
  renderParams.draw_water = _draw_water.get();
  renderParams.draw_wmo_doodads = _draw_wmo_doodads.get();
  renderParams.draw_models = _draw_models.get();
  renderParams.draw_model_animations = _draw_model_animations.get();
  renderParams.draw_models_with_box = _draw_models_with_box.get();
  renderParams.draw_hidden_models = _draw_hidden_models.get();
  renderParams.draw_sky = _draw_sky.get();
  renderParams.draw_skybox = _draw_skybox.get();
  renderParams.draw_fog = _draw_fog.get();
  renderParams.ground_editing_brush = terrainType;
  renderParams.water_layer = displayed_water_layer;
  renderParams.display_mode = _display_mode;
  renderParams.draw_occlusion_boxes = _draw_occlusion_boxes.get();
  renderParams.minimap_render = false;
  renderParams.draw_chunk_manipulator_selection = (terrainMode == editing_mode::chunk);
  renderParams.draw_wmo_exterior = _draw_wmo_exterior.get();
  renderParams.render_select_m2_aabb = _render_m2_aabb;
  renderParams.render_select_m2_collission_bbox = _render_m2_collission_bbox;
  renderParams.render_select_wmo_aabb = _render_wmo_aabb;
  renderParams.render_select_wmo_groups_bounds = _render_wmo_groups_bounds;

  if (terrainMode == editing_mode::mccv && _mod_alt_down && _draw_terrain.get())
  {
    // tick() runs after draw_map(), so _cursor_pos can lag _last_mouse_pos by a frame.
    // Refresh here so the hub / NDC crosshair match the current ray hit (same rules as tick() for mccv).
    {
      auto cur_action = NOGGIT_CUR_ACTION;
      if (((cur_action && !cur_action->getBlockCursor()) || !cur_action) && !_locked_cursor_mode.get())
      {
        update_cursor_pos();
      }
    }

    glm::vec4 const clip = _projection * _model_view * glm::vec4(_cursor_pos, 1.f);
    if (clip.w > 1e-4f)
    {
      glm::vec3 const ndc = glm::vec3(clip) / clip.w;
      renderParams.draw_mccv_vertex_alt_viz = true;
      renderParams.mccv_viz_hub = _cursor_pos;
      renderParams.mccv_viz_radius = radius;
      renderParams.mccv_viz_hub_valid = true;
      renderParams.mccv_viz_hub_ndc = glm::vec2(ndc.x, ndc.y);
    }
  }

  if (_ramp_tool_window && _ramp_tool_window->isVisible()
      && _ramp_point_a && _ramp_point_b
      && _display_mode == display_mode::in_3D && _draw_terrain.get())
  {
    glm::vec2 const rd(_ramp_point_b->x - _ramp_point_a->x, _ramp_point_b->z - _ramp_point_a->z);
    float const r_tool = _ramp_tool_window->radius();
    if (glm::length(rd) > 1e-2f && r_tool > 0.f)
    {
      renderParams.draw_ramp_preview = true;
      renderParams.ramp_preview_a = *_ramp_point_a;
      renderParams.ramp_preview_b = *_ramp_point_b;
      renderParams.ramp_preview_radius = r_tool;
      renderParams.ramp_preview_cap_len = _ramp_tool_window->capLength();
    }
  }

  Noggit::register_crash_render_stage("MapView::draw_map:WorldRender::draw");
  _world->renderer()->draw (
                  _model_view
                , _projection
                , _cursor_pos
                , cursorColor
                , ref_pos
                , _camera.position
                , &minimapRenderSettings
                , renderParams
                );
  Noggit::register_crash_render_stage("MapView::draw_map:WorldRender::draw_done");

  // reset after each world::draw call
  _camera_moved_since_last_draw = false;

  Noggit::register_crash_render_stage("MapView::draw_map:exit");
}

void MapView::keyPressEvent (QKeyEvent *event)
{
  if (event->key() == Qt::Key_Space)
  {
    _mod_space_down = true;
  }

  size_t const modifier
    ( ((event->modifiers() & Qt::ShiftModifier) ? MOD_shift : 0)
    | ((event->modifiers() & Qt::ControlModifier) ? MOD_ctrl : 0)
    | ((event->modifiers() & Qt::AltModifier) ? MOD_alt : 0)
    | ((event->modifiers() & Qt::MetaModifier) ? MOD_meta : 0)
    | ((event->modifiers() & Qt::KeypadModifier) ? MOD_num : 0)
    | (_mod_space_down ? MOD_space : 0)
    );

  for (auto&& hotkey : hotkeys)
  {
    if (event->key() == hotkey.key && modifier == hotkey.modifiers && hotkey.condition())
    {
      makeCurrent();
      OpenGL::context::scoped_setter const _ (::gl, context());

      hotkey.onPress();
      return;
    }
  }

  checkInputsSettings();

  // movement
  if (event->key() == _inputs[0])
  {
    moving = 1.0f;
  }
  if (event->key() == _inputs[1])
  {
    moving = -1.0f;
  }

  if (event->key() == Qt::Key_Up)
  {
    lookat = 0.75f;
  }
  if (event->key() == Qt::Key_Down)
  {
    lookat = -0.75f;
  }

  if (event->key() == Qt::Key_Right)
  {
    turn = 0.75f;
  }
  if (event->key() == Qt::Key_Left)
  {
    turn = -0.75f;
  }

  if (event->key() == _inputs[2])
  {
    strafing = 1.0f;
  }
  if (event->key() == _inputs[3])
  {
    strafing = -1.0f;
  }

  if (event->key() == _inputs[4])
  {
    updown = 1.0f;
  }
  if (event->key() == _inputs[5])
  {
    updown = -1.0f;
  }

  if (event->key() == Qt::Key_Home)
  {
	  _camera.position = glm::vec3(_cursor_pos.x, _cursor_pos.y + 50, _cursor_pos.z);
    _camera_moved_since_last_draw = true;
  }

  if (event->key() == Qt::Key_L)
  {
    freelook = true;
  }

  if (_display_mode == display_mode::in_2D)
  {
    TileIndex cur_tile = TileIndex(_camera.position);

    if (event->key() == Qt::Key_Up)
    {
      auto next_z = cur_tile.z - 1;
      _camera.position = glm::vec3((cur_tile.x * TILESIZE) + (TILESIZE / 2), _camera.position.y, (next_z * TILESIZE) + (TILESIZE / 2));
      _camera_moved_since_last_draw = true;
    }
    else if (event->key() == Qt::Key_Down)
    {
      auto next_z = cur_tile.z + 1;
      _camera.position = glm::vec3((cur_tile.x * TILESIZE) + (TILESIZE / 2), _camera.position.y, (next_z * TILESIZE) + (TILESIZE / 2));
      _camera_moved_since_last_draw = true;
    }
    else if (event->key() == Qt::Key_Left)
    {
      auto next_x = cur_tile.x - 1;
      _camera.position = glm::vec3((next_x * TILESIZE) + (TILESIZE / 2), _camera.position.y, (cur_tile.z * TILESIZE) + (TILESIZE / 2));
      _camera_moved_since_last_draw = true;
    }
    else if (event->key() == Qt::Key_Right)
    {
      auto next_x = cur_tile.x + 1;
      _camera.position = glm::vec3((next_x * TILESIZE) + (TILESIZE / 2), _camera.position.y, (cur_tile.z * TILESIZE) + (TILESIZE / 2));
      _camera_moved_since_last_draw = true;
    }

  }

  if (_gizmo_on.get() && !_transform_gizmo.isUsing())
  {
    if (!_change_operation_mode && event->key() == Qt::Key_Space)
    {
      if (_gizmo_operation == ImGuizmo::OPERATION::TRANSLATE)
      {
        updateGizmoOverlay(ImGuizmo::OPERATION::ROTATE);
      }
      else if (_gizmo_operation == ImGuizmo::OPERATION::ROTATE)
      {
        updateGizmoOverlay(ImGuizmo::OPERATION::SCALE);
      }
      else
      {
        updateGizmoOverlay(ImGuizmo::OPERATION::TRANSLATE);
      }

      _change_operation_mode = true;
    }
  }
}

void MapView::keyReleaseEvent (QKeyEvent* event)
{
  if (event->key() == Qt::Key_Space)
    _mod_space_down = false;

  if (_change_operation_mode && event->key() == Qt::Key_Space)
    _change_operation_mode = false;

  checkInputsSettings();

  size_t const modifier
  (((event->modifiers() & Qt::ShiftModifier) ? MOD_shift : 0)
      | ((event->modifiers() & Qt::ControlModifier) ? MOD_ctrl : 0)
      | ((event->modifiers() & Qt::AltModifier) ? MOD_alt : 0)
      | ((event->modifiers() & Qt::MetaModifier) ? MOD_meta : 0)
      | ((event->modifiers() & Qt::KeypadModifier) ? MOD_num : 0)
      | (_mod_space_down ? MOD_space : 0)
  );
  for (auto&& hotkey : hotkeys)
  {
      auto k = event->key();
      if (k == hotkey.key && modifier == hotkey.modifiers && hotkey.condition())
      {
          makeCurrent();
          OpenGL::context::scoped_setter const _(::gl, context());

          hotkey.onRelease();
          return;
      }
  }

  // movement
  if (event->key() == _inputs[0] || event->key() == _inputs[1])
  {
    moving = 0.0f;
  }

  if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)
  {
    lookat = 0.0f;
  }

  if (event->key() == Qt::Key_Right || event->key() == Qt::Key_Left)
  {
    turn  = 0.0f;
  }

  if (event->key() == _inputs[2] || event->key() == _inputs[3])
  {
    strafing  = 0.0f;
  }

  if (event->key() == _inputs[4] || event->key() == _inputs[5])
  {
    updown  = 0.0f;
  }

  if (event->key() == Qt::Key_L || event->key() == Qt::Key_Minus)
  {
    freelook = false;
  }

}

void MapView::checkInputsSettings()
{
  QString _locale = _settings->value("keyboard_locale", "QWERTY").toString();

  // default is QWERTY
  _inputs = std::array<Qt::Key, 6>{Qt::Key_W, Qt::Key_S, Qt::Key_D, Qt::Key_A, Qt::Key_Q, Qt::Key_E};

  if (_locale == "AZERTY")
  {
      _inputs = std::array<Qt::Key, 6>{Qt::Key_Z, Qt::Key_S, Qt::Key_D, Qt::Key_Q, Qt::Key_A, Qt::Key_E};
  }
}

void MapView::focusOutEvent (QFocusEvent*)
{
  _mod_alt_down = false;
  _mod_ctrl_down = false;
  _mod_shift_down = false;
  _mod_space_down = false;
  _mod_num_down = false;

  moving = 0.0f;
  lookat = 0.0f;
  turn = 0.0f;
  strafing = 0.0f;
  updown = 0.0f;

  leftMouse = false;
  rightMouse = false;
  middleMouse = false;
  look = false;
  freelook = false;

  if (_point_light_mmb_edit_action)
  {
    if (NOGGIT_CUR_ACTION)
      NOGGIT_ACTION_MGR->endAction();
    _point_light_mmb_edit_action = false;
  }

  activeTool()->onFocusLost();
}

void MapView::mouseMoveEvent (QMouseEvent* event)
{
  //! \todo:  move the function call requiring a context in tick ?
  makeCurrent();
  OpenGL::context::scoped_setter const _ (::gl, context());
  QLineF const relative_movement (_last_mouse_pos, event->pos());

  if ((look || freelook) && !(_mod_shift_down || _mod_ctrl_down || _mod_alt_down || _mod_space_down))
  {
    _camera.add_to_yaw(math::degrees(relative_movement.dx() / XSENS));
    _camera.add_to_pitch(math::degrees(mousedir * relative_movement.dy() / YSENS));
    _camera_moved_since_last_draw = true;
  }

  Noggit::MouseMoveParameters params{
    .displayMode = _display_mode,
    .left_mouse = leftMouse,
    .right_mouse = rightMouse,
    .mod_shift_down = _mod_shift_down,
    .mod_ctrl_down = _mod_ctrl_down,
    .mod_alt_down = _mod_alt_down,
    .mod_num_down = _mod_num_down,
    .mod_space_down = _mod_space_down,
    .relative_movement = relative_movement,
    .mouse_position = event->pos()
  };

  activeTool()->onMouseMove(params);

  if (_display_mode == display_mode::in_2D && leftMouse && _mod_alt_down && _mod_shift_down)
  {
    strafing = ((relative_movement.dx() / XSENS) / -1) * 5.0f;
    moving = (relative_movement.dy() / YSENS) * 5.0f;
  }

  if (_display_mode == display_mode::in_2D && rightMouse && _mod_shift_down)
  {
    updown = (relative_movement.dy() / YSENS);
  }

  _last_mouse_pos = event->pos();
}

void MapView::change_selected_wmo_nameset(int set)
{
    auto last_entry = _world->get_last_selected_model();
    if (last_entry)
    {
        if (last_entry.value().index() != eEntry_Object)
        {
            return;
        }
        auto obj = std::get<selected_object_type>(last_entry.value());
        if (obj->which() == eWMO)
        {
            WMOInstance* wmo = static_cast<WMOInstance*>(obj);
            wmo->change_nameset(set);
            _world->updateTilesWMO(wmo, model_update::none); // needed?
            auto tiles = wmo->getTiles();
            for (auto tile : tiles)
            {
                tile->changed = true;
            }
        }
    }
}

void MapView::change_selected_wmo_doodadset(int set)
{
  for (auto& selection : _world->current_selection())
  {
    if (selection.index() != eEntry_Object)
      continue;

    auto obj = std::get<selected_object_type>(selection);

    if (obj->which() == eWMO)
    {
      auto wmo = static_cast<WMOInstance*>(obj);
      wmo->change_doodadset(set);
      _world->updateTilesWMO(wmo, model_update::none);
      auto tiles = wmo->getTiles();
      for (auto tile : tiles)
      {
        tile->changed = true;
      }
    }
  }
}

void MapView::mousePressEvent(QMouseEvent* event)
{
  if(event->source() == Qt::MouseEventNotSynthesized)
  {
    _tablet_manager->setIsActive(false);
  }

  _last_mouse_pos = event->pos();

  makeCurrent();
  OpenGL::context::scoped_setter const _(::gl, context());

  activeTool()->onMousePress({
      .button = event->button(),
      .mouse_position = event->pos(),
      .mod_shift_down = _mod_shift_down,
      .mod_ctrl_down = _mod_ctrl_down,
      .mod_alt_down = _mod_alt_down,
      .mod_num_down = _mod_num_down,
      .mod_space_down = _mod_space_down,
      });

  switch (event->button())
  {
  case Qt::LeftButton:
    leftMouse = true;
    // Commit world pick for point lights only in the point light tool (per-frame hover uses mouseMove=true there).
    if (_display_mode == display_mode::in_3D && !_locked_cursor_mode.get()
        && terrainMode == editing_mode::point_light)
    {
      doSelection(true, false);
    }
    break;

  case Qt::RightButton:
    rightMouse = true;
    break;

  case Qt::MiddleButton:
    middleMouse = true;
    break;

  default:
    break;
  }

  if (leftMouse && terrainMode == editing_mode::minimap && !_mod_ctrl_down)
  {
      _drag_start_pos = event->pos();
      _needs_redraw = true;
  }

  if (rightMouse)
  {
    _right_click_pos = event->pos();
    look = true;
  }
}

void MapView::wheelEvent (QWheelEvent* event)
{
  //! \todo: move the function call requiring a context in tick ?
  makeCurrent();
  OpenGL::context::scoped_setter const _ (::gl, context());

  auto&& delta_for_range
    ( [&] (float range)
      {
        //! \note / 8.f for degrees, / 40.f for smoothness
        return (_mod_ctrl_down ? 0.01f : 0.1f) 
          * range 
          // alt = horizontal delta
          * (_mod_alt_down ? event->angleDelta().x() : event->angleDelta().y())
          / 320.f
          ;
      }
    );

  Noggit::MouseWheelParameters params
  {
      .event = *event,
      .mod_shift_down = _mod_shift_down,
      .mod_ctrl_down = _mod_ctrl_down,
      .mod_alt_down = _mod_alt_down,
      .mod_num_down = _mod_num_down,
      .mod_space_down = _mod_space_down,
  };
  activeTool()->onMouseWheel(params);
}

void MapView::mouseReleaseEvent (QMouseEvent* event)
{
  makeCurrent();
  OpenGL::context::scoped_setter const _(::gl, context());

  _last_mouse_pos = event->pos();

  activeTool()->onMouseRelease(
  {
      .button = event->button(),
      .mouse_position = event->pos(),
      .mod_ctrl_down = _mod_ctrl_down,
  });

  if (event->button() == Qt::LeftButton && _display_mode == display_mode::in_3D
      && !ImGuizmo::IsUsing() && _ramp_pick_target != 0)
  {
    tryRampTerrainPick(event->pos(), event->button());
  }

  switch (event->button())
  {
  case Qt::LeftButton:
    leftMouse = false;

    if (_display_mode == display_mode::in_2D)
    {
      strafing = 0;
      moving = 0;
    }

    // World point-light picking is handled in MapView::doSelection() when terrainMode == editing_mode::point_light.

    if (terrainMode == editing_mode::minimap )
    {
        if (!_mod_ctrl_down)
        {
            auto drag_end_pos = event->pos();

            if (_drag_start_pos != drag_end_pos && !ImGuizmo::IsUsing())
            {
                const std::array<glm::vec2, 2> selection_box
                {
                    glm::vec2(std::min(_drag_start_pos.x(), drag_end_pos.x()), std::min(_drag_start_pos.y(), drag_end_pos.y())),
                    glm::vec2(std::max(_drag_start_pos.x(), drag_end_pos.x()), std::max(_drag_start_pos.y(), drag_end_pos.y()))
                };
                // _world->select_objects_in_area(selection_box, !_mod_shift_down, model_view(), projection(), width(), height(), objectEditor->drag_selection_depth(), _camera.position);
                _world->select_objects_in_area(selection_box, !_mod_shift_down, _model_view, _projection, width(), height(), 50000.0f, _camera.position);
            }
            else // Do normal selection when we just clicked
            {
                doSelection(false);
            }
        }
        else
        {
            doSelection(true);
        }
    }

    break;

  case Qt::RightButton:
    rightMouse = false;

    look = false;

    if (_display_mode == display_mode::in_2D)
      updown = 0;

    // // may need to be done in constructor of widget
    // this->setContextMenuPolicy(Qt::CustomContextMenu); 
    // connect(this, SIGNAL(customContextMenuRequested(const QPoint&)),
    //     this, SLOT(ShowContextMenu(const QPoint&)));



    break;

  case Qt::MiddleButton:
    middleMouse = false;
    if (_point_light_mmb_edit_action)
    {
      if (NOGGIT_CUR_ACTION)
        NOGGIT_ACTION_MGR->endAction();
      _point_light_mmb_edit_action = false;
    }
    break;

  default:
    break;
  }
}

void MapView::save(save_mode mode)
{
  bool save = true;

  activeTool()->saveSettings();

  if (AsyncLoader::instance->important_object_failed_loading())
  {
    save = false;
    QPushButton *yes, *no;

    QMessageBox first_warning;
    first_warning.setIcon(QMessageBox::Critical);
    first_warning.setWindowIcon(QIcon (":/icon"));
    first_warning.setWindowTitle("Some models couldn't be loaded");
    first_warning.setText("Error:\nSome models could not be loaded and saving will cause collision and culling issues,"
      " this is most likely caused by missing or corrupted models."
      "\nCheck the log file for the list of model errors and fix them."
      "\nWould you still like to save ?");
    // roles are swapped to force the user to pay attention and both are "accept" roles so that escape does nothing
    no = first_warning.addButton("No", QMessageBox::ButtonRole::AcceptRole);
    yes = first_warning.addButton("Yes", QMessageBox::ButtonRole::YesRole);
    first_warning.setDefaultButton(no);

    first_warning.exec();

    if (first_warning.clickedButton() == yes)
    {
      QMessageBox second_warning;
      second_warning.setIcon(QMessageBox::Warning);
      second_warning.setWindowIcon(QIcon (":/icon"));
      second_warning.setWindowTitle("Are you sure ?");
      second_warning.setText( "If you save you will have to save again all the adt containing the defective/missing models once you've fixed said models to correct all the issues.\n"
                              "By clicking yes you accept to bear all the consequences of your action and forfeit the right to complain to the developers about any culling and collision issues.\n\n"
                              "So... do you REALLY want to save ?"
                            );
      no = second_warning.addButton("No", QMessageBox::ButtonRole::YesRole);
      yes = second_warning.addButton("Yes", QMessageBox::ButtonRole::AcceptRole);
      second_warning.setDefaultButton(no);

      second_warning.exec();

      if (second_warning.clickedButton() == yes)
      {
        save = true;
      }
    }
  }

  if ( mode == save_mode::current 
    && save 
    && (QMessageBox::warning
          (nullptr
          , "Save current map tile only"
          , "This can cause a collision bug when placing objects between two ADT borders!\n\n"
            "We recommend you to use the normal save function rather than "
            "this one to get the collisions right."
          , QMessageBox::Save | QMessageBox::Cancel
          , QMessageBox::Cancel
          ) == QMessageBox::Cancel
       )
     )
  {
    save = false;
  }

  if (save)
  {
    makeCurrent();
    OpenGL::context::scoped_setter const _ (::gl, context());

    switch (mode)
    {
    case save_mode::current: _world->mapIndex.saveTile(TileIndex(_camera.position), _world.get()); break;
    case save_mode::changed: _world->mapIndex.saveChanged(_world.get()); break;
    case save_mode::all:     _world->mapIndex.saveall(_world.get()); break;
    }
    // write wdl, we update wdl data prior in the mapIndex saving fucntions above
    _world->horizon.save_wdl(_world.get());

    if (auto const epsilon_cfg = Noggit::Integrations::EpsilonPatchExporter::load_config_from_settings())
    {
      if (auto* const app = Noggit::Application::NoggitApplication::instance();
          app && app->hasClientData())
      {
        Noggit::Integrations::EpsilonPatchExporter::instance().export_map (
          *epsilon_cfg, _world->basename, app->clientData());
      }
    }

    for (auto&& dbc : _dirty_dbcs)
    {
      dbc->save();
    }

    NOGGIT_ACTION_MGR->purge();
    AsyncLoader::instance->reset_object_fail();

    _main_window->statusBar()->showMessage("Map saved", 2000);

  }
  else
  {
    QMessageBox::warning
      ( nullptr
      , "Map NOT saved"
      , "The map was NOT saved, don't forget to save before leaving"
      , QMessageBox::Ok
      );
  }
}

void MapView::addHotkey(Qt::Key key, size_t modifiers, std::function<void()> function, std::function<bool()> condition)
{
  hotkeys.emplace_front (key, modifiers, function, condition);
}

void MapView::addHotkey(Qt::Key key, size_t modifiers, StringHash hotkeyName)
{
  // Compare raw hashes: StringHash::operator== is consteval and cannot be used from non-consteval lambdas (MSVC).
  std::uint64_t const name = hotkeyName.hash;
  constexpr std::uint64_t h_move_down = "moveSelectedDown"_hash.hash;
  constexpr std::uint64_t h_move_up = "moveSelectedUp"_hash.hash;
  constexpr std::uint64_t h_move_left = "moveSelectedLeft"_hash.hash;
  constexpr std::uint64_t h_move_right = "moveSelectedRight"_hash.hash;

  auto const is_point_light_numpad_move = [=]()
  {
    if (terrainMode != editing_mode::point_light || !_world || !_world->selectedPointLightIndex().has_value())
      return false;

    return name == h_move_down || name == h_move_up || name == h_move_left || name == h_move_right;
  };

  auto const apply_point_light_move_axis = [=](bool press)
  {
    if (name == h_move_down)
      _point_light_keyx = press ? 1.f : 0.f;
    else if (name == h_move_up)
      _point_light_keyx = press ? -1.f : 0.f;
    else if (name == h_move_left)
      _point_light_keyz = press ? 1.f : 0.f;
    else if (name == h_move_right)
      _point_light_keyz = press ? -1.f : 0.f;
  };

  hotkeys.emplace_front(
      key,
      modifiers,
      [=]
      {
        if (is_point_light_numpad_move())
          apply_point_light_move_axis(true);
        else
          activeTool()->onHotkeyPress(hotkeyName);
      },
      [=]
      {
        if (is_point_light_numpad_move())
          return true;
        return activeTool()->hotkeyCondition(hotkeyName);
      },
      [=]
      {
        if (is_point_light_numpad_move())
          apply_point_light_move_axis(false);
        else
          activeTool()->onHotkeyRelease(hotkeyName);
      });
}

void MapView::unloadOpenglData()
{
  makeCurrent();
  OpenGL::context::scoped_setter const _ (::gl, context());

  ModelManager::unload_all(_context);
  WMOManager::unload_all(_context);
  TextureManager::unload_all(_context);

  for (MapTile* tile : _world->mapIndex.loaded_tiles())
  {
    tile->renderer()->unload();
    tile->Water.renderer()->unload();

    for (int i = 0; i < 16; ++i)
    {
      for (int j = 0; j < 16; ++j)
      {
        tile->getChunk(i, j)->unload();
      }
    }
  }

  _world->renderer()->unload();

  _buffers.unload();
  _gl_initialized = false;
}

QWidget* MapView::getSecondaryToolBar()
{
    return _viewport_overlay_ui->secondaryToolbarHolder;
}

QWidget* MapView::getLeftSecondaryToolbar()
{
    return _viewport_overlay_ui->leftSecondaryToolbarHolder;
}

[[nodiscard]]
Noggit::NoggitRenderContext MapView::getRenderContext()
{
  return _context;
}

[[nodiscard]]
World* MapView::getWorld() const
{
  return _world.get();
}

[[nodiscard]]
QDockWidget* MapView::getAssetBrowser()
{
  return _asset_browser_dock;
}

[[nodiscard]]
Noggit::Ui::Tools::AssetBrowser::Ui::AssetBrowserWidget* MapView::getAssetBrowserWidget()
{
  return _asset_browser;
}

glm::vec3 MapView::cursorPosition() const
{
    return _cursor_pos;
}

void MapView::cursorPosition(glm::vec3 position)
{
    _cursor_pos = position;
}

void MapView::setRampPickTarget(int which)
{
  _ramp_pick_target = std::clamp(which, 0, 2);
  if (_ramp_tool_window)
  {
    _ramp_tool_window->refreshPointLabels();
  }
  invalidate();
}

int MapView::rampPickTarget() const
{
  return _ramp_pick_target;
}

std::optional<glm::vec3> MapView::rampPointA() const
{
  return _ramp_point_a;
}

std::optional<glm::vec3> MapView::rampPointB() const
{
  return _ramp_point_b;
}

void MapView::clearRampPoints()
{
  _ramp_point_a.reset();
  _ramp_point_b.reset();
  _ramp_pick_target = 0;
}

void MapView::ensureRampToolWindow()
{
  if (!_ramp_tool_window)
  {
    _ramp_tool_window = new Noggit::Ui::RampCreationTool(this, _main_window);
    QObject::connect(_ramp_tool_window, &QObject::destroyed, this, [this]
    {
      _ramp_tool_window = nullptr;
    });
    QObject::connect(_ramp_tool_window, &Noggit::Ui::RampCreationTool::createRampRequested,
                     this, &MapView::onRampCreateRequested);
  }
  _ramp_tool_window->refreshPointLabels();
  _ramp_tool_window->show();
  _ramp_tool_window->raise();
  _ramp_tool_window->activateWindow();
}

void MapView::tryRampTerrainPick(QPoint const& mouse_px, Qt::MouseButton button)
{
  if (button != Qt::LeftButton || _ramp_pick_target == 0)
  {
    return;
  }
  if (!_draw_terrain.get())
  {
    return;
  }

  selection_result const results = intersect_result(true, mouse_px);
  if (results.empty())
  {
    return;
  }

  auto const& hit = results.front().second;
  if (hit.index() != eEntry_MapChunk)
  {
    return;
  }

  glm::vec3 const pos = std::get<selected_chunk_type>(hit).position;
  if (_ramp_pick_target == 1)
  {
    _ramp_point_a = pos;
  }
  else if (_ramp_pick_target == 2)
  {
    _ramp_point_b = pos;
  }
  _ramp_pick_target = 0;

  if (_ramp_tool_window)
  {
    _ramp_tool_window->refreshPointLabels();
  }
  invalidate();
}

void MapView::onRampCreateRequested()
{
  if (!_ramp_tool_window || !_ramp_point_a || !_ramp_point_b)
  {
    return;
  }

  glm::vec2 const d(_ramp_point_b->x - _ramp_point_a->x, _ramp_point_b->z - _ramp_point_a->z);
  if (glm::length(d) < 1e-2f)
  {
    QMessageBox::warning(this, tr("Ramp"), tr("Start and end are too close in XZ."));
    return;
  }

  float const r = _ramp_tool_window->radius();
  if (r <= 0.f)
  {
    return;
  }

  if (NOGGIT_CUR_ACTION)
  {
    QMessageBox::information(this, tr("Ramp"), tr("Finish the current action before creating a ramp."));
    return;
  }

  makeCurrent();
  OpenGL::context::scoped_setter const _(::gl, context());

  if (Noggit::Action* action = NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNKS_TERRAIN,
                                                               Noggit::ActionModalityControllers::eNONE))
  {
    _world->applyTerrainRamp(*_ramp_point_a, *_ramp_point_b, r,
                             _ramp_tool_window->capLength(), _ramp_tool_window->blendStrength());
    NOGGIT_ACTION_MGR->endAction();
  }

  invalidate();
}

void MapView::runFixAllTerrainGapsUndoable()
{
  if (NOGGIT_CUR_ACTION)
  {
    QMessageBox::information(this, tr("Fix gaps"), tr("Finish the current action before fixing seams."));
    return;
  }
  if (_display_mode != display_mode::in_3D)
  {
    QMessageBox::information(this, tr("Fix gaps"), tr("Switch to 3D view to fix terrain seams."));
    return;
  }

  makeCurrent();
  OpenGL::context::scoped_setter const _(::gl, context());

  if (Noggit::Action* action = NOGGIT_ACTION_MGR->beginAction(this, Noggit::ActionFlags::eCHUNKS_TERRAIN,
                                                               Noggit::ActionModalityControllers::eNONE))
  {
    (void)action;
    _world->fixAllGaps();
    NOGGIT_ACTION_MGR->endAction();
  }

  invalidate();
}

void MapView::enableGizmoBar()
{
  _viewport_overlay_ui->gizmoBar->show();
}

void MapView::disableGizmoBar()
{
  _viewport_overlay_ui->gizmoBar->hide();
}

void MapView::setDbcDirty(DBCFile* dbc)
{
  for (auto&& dirty_dbc : _dirty_dbcs)
  {
    if (dirty_dbc == dbc)
    {
      return;
    }
  }

  _dirty_dbcs.emplace_back(dbc);
}

// also called when loading world/viewport in MapView::initializeGL()
void MapView::onSettingsSave()
{
  _classic_ui = _settings->value("classicUI", false).toBool();
  _tablet_pressure_strength = _settings->value("tablet/pressure_strength", true).toBool();

  OpenGL::TerrainParamsUniformBlock* params = _world->renderer()->getTerrainParamsUniformBlock();
  params->draw_tileset = _draw_tileset.get();
  params->draw_texture_layer_count_overlay = _draw_texture_layer_count_overlay.get();
  params->wireframe_type = _settings->value("wireframe/type", false).toBool();
  params->wireframe_radius = _settings->value("wireframe/radius", 1.5f).toFloat();
  params->wireframe_width = _settings->value ("wireframe/width", 1.f).toFloat();

  /* temporaryyyyyy */
  params->climb_value = 1.0f;

  QColor c = _settings->value("wireframe/color").value<QColor>();
  glm::vec4 wireframe_color(c.redF(), c.greenF(), c.blueF(), c.alphaF());
  params->wireframe_color = wireframe_color;

  _world->renderer()->directional_lightning = _settings->value("directional_lightning", true).toBool();
  _world->renderer()->local_lightning = _settings->value("local_lightning", true).toBool();

  // refresh rendering
  _world->renderer()->markTerrainParamsUniformBlockDirty();
  _world->renderer()->skies()->force_update();

  _world->renderer()->_view_distance = _settings->value("view_distance", 2000.f).toFloat() + TILE_RADIUS;
  _world.get()->mapIndex.setLoadingRadius(_settings->value("loading_radius", 2).toInt());
  _world.get()->mapIndex.setUnloadDistance(_settings->value("unload_dist", 5).toInt());
  _world.get()->mapIndex.setUnloadInterval(_settings->value("unload_interval", 30).toInt());

  _camera.fov(math::degrees(_settings->value("fov", 54.f).toFloat()));
  _debug_cam.fov(math::degrees(_settings->value("fov", 54.f).toFloat()));

  int _fps_limit = _settings->value("fps_limit", 60).toInt();
  int _frametime = static_cast<int>((1.f / static_cast<float>(_fps_limit)) * 1000.f);
  // _update_every_event_loop.start(_frametime);
  _update_every_event_loop.setInterval(_frametime);

  bool vsync = _settings->value("vsync", false).toBool();
  format().setSwapInterval(vsync ? 1 
                           : Noggit::Application::NoggitApplication::instance()->getConfiguration()->GraphicsConfiguration.SwapChainInternal);

  bool doAntiAliasing = _settings->value("anti_aliasing", false).toBool();
  format().setSamples(doAntiAliasing ? 4 
                      : Noggit::Application::NoggitApplication::instance()->getConfiguration()->GraphicsConfiguration.SamplesCount);

  _render_m2_aabb = _settings->value("render/m2_aabb", false).toBool();
  _render_m2_collission_bbox = _settings->value("render/m2_coll_bb", false).toBool();
  _render_wmo_aabb = _settings->value("render/wmo_aabb", false).toBool();
  _render_wmo_groups_bounds = _settings->value("render/wmo_groups_bounds", false).toBool();

  // force updating rendering
  _camera_moved_since_last_draw = true;

  auto app_config = Noggit::Application::NoggitApplication::instance()->getConfiguration();
  app_config->modern_features = _settings->value("modern_features", false).toBool();

  bool const new_discord_enabled = _settings->value("integrations/discord_rich_presence", false).toBool();
  std::string const new_discord_app_id = _settings->value("integrations/discord_app_id", "959654402141085748").toString().toStdString();
  std::string const new_discord_large_key = _settings->value("integrations/discord_large_image_key", "").toString().toStdString();
  std::string const new_discord_large_text = _settings->value("integrations/discord_large_image_text", "Noggit Red").toString().toStdString();

  if (new_discord_enabled != _discord_rich_presence_enabled
   || new_discord_app_id != _discord_rich_presence_app_id
   || new_discord_large_key != _discord_rich_presence_large_image_key
   || new_discord_large_text != _discord_rich_presence_large_image_text)
  {
    _discord_rich_presence_enabled = new_discord_enabled;
    _discord_rich_presence_app_id = new_discord_app_id;
    _discord_rich_presence_large_image_key = new_discord_large_key;
    _discord_rich_presence_large_image_text = new_discord_large_text;
    _discord_rich_presence_start_ts = std::nullopt;
    _discord_last_map.clear();
    _discord_last_tool.clear();

    if (_discord_rich_presence_enabled)
    {
      Noggit::Integrations::DiscordRichPresence::instance().configure(true, _discord_rich_presence_app_id);
      Noggit::Integrations::DiscordRichPresence::instance().setAssets(
        Noggit::Integrations::DiscordAssets{ _discord_rich_presence_large_image_key, _discord_rich_presence_large_image_text }
      );
    }
    else
      Noggit::Integrations::DiscordRichPresence::instance().shutdown();
  }

}

void MapView::setCameraDirty()
{
  _camera_moved_since_last_draw = true;
}

[[nodiscard]]
Noggit::Ui::minimap_widget* MapView::getMinimapWidget() const
{
  return _minimap;
}

void MapView::ShowContextMenu(QPoint pos) 
{
    // QApplication::startDragDistance() is 10
    bool mouse_moved = (QApplication::startDragDistance() / 5) < (_right_click_pos - pos).manhattanLength();

    // don't show context menu if dragging mouse
    if (mouse_moved || ImGuizmo::IsUsing())
        return;

    // TODO : build the menu only once, store it and instead use setVisible ?

    QMenu* menu = new QMenu(this);

    // Undo
    QAction action_undo("Undo", this);
    menu->addAction(&action_undo);
    action_undo.setShortcut(QKeySequence::Undo);
    QObject::connect(&action_undo, &QAction::triggered, [=]()
        {
            NOGGIT_ACTION_MGR->undo();
        });
    // Redo
    QAction action_redo("Redo", this);
    menu->addAction(&action_redo);
    action_redo.setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));
    QObject::connect(&action_redo, &QAction::triggered, [=]()
        {
            NOGGIT_ACTION_MGR->redo();
        });

    activeTool()->registerContextMenuItems(menu);

    menu->exec(mapToGlobal(pos)); // synch
    // menu->popup(mapToGlobal(pos)); // asynch, needs to be preloaded to work
}

void MapView::onApplicationStateChanged(Qt::ApplicationState state)
{
    // auto interval = _update_every_event_loop.interval();

    if (!_settings->value("background_fps_limit", true).toBool())
        return;

    int fps_limit = _settings->value("fps_limit", 60).toInt();
    int fps_calcul = (int)((1.f / (float)fps_limit) * 1000.f);

    switch (state)
    {
    case Qt::ApplicationState::ApplicationHidden:
    {
        // The application is hidden and runs in the background.
        // this isn't minimized, it's when the window is entirely hidden, should never happen on noggit
        _update_every_event_loop.setInterval(1000); // set to 1fps
        break;
    }
    case Qt::ApplicationState::ApplicationActive:
    {
        _update_every_event_loop.setInterval(fps_calcul); // normal
        break;
    }
    case Qt::ApplicationState::ApplicationInactive:
    {
        // The application is visible, but not selected to be in front.
        _update_every_event_loop.setInterval(fps_calcul * 2); // half fps if inactive
        break;
    }
    case Qt::ApplicationState::ApplicationSuspended:
    {
        // don't run updates ?
        _update_every_event_loop.setInterval(1000);
        break;
    }
    default:
        break;
    }
}
