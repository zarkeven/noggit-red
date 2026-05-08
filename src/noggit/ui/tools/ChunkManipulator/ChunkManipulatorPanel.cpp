// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include "ChunkManipulatorPanel.hpp"

#include "ChunkCopyOptionsWidget.hpp"
#include "ChunkGroupsWidget.hpp"

#include <noggit/MapView.h>
#include <noggit/tools/ChunkTool.hpp>
#include <noggit/World.h>

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QSlider>
#include <QVBoxLayout>

using namespace Noggit::Ui::Tools::ChunkManipulator;

ChunkManipulatorPanel::ChunkManipulatorPanel(MapView* map_view, Noggit::ChunkTool* tool, QWidget* parent)
  : QWidget(parent)
  , _map_view(map_view)
  , _tool(tool)
{
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  setToolTip(
    tr("Shift+LMB drag: add to quick selection (releasing Shift or LMB copies the selection to the clipboard). "
       "Ctrl+LMB drag: remove. Alt+LMB or Alt+RMB drag: brush radius.\n"
       "V: paste at cursor (channels depend on Paste Allow). Paste height offset shifts pasted terrain, liquid heights, and point lights on Y.\n"
       "Right-click the group list to save the current selection or delete a group."));

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(6);

  QString map_key = QString::fromStdString(map_view->getWorld()->basename);
  map_key.replace(QLatin1Char('/'), QLatin1Char('_'));

  if (auto* clip = map_view->getWorld()->chunkClipboard())
  {
    {
      auto* selector = new QGroupBox(tr("Selector"), this);
      auto* sel_l = new QVBoxLayout(selector);
      sel_l->setContentsMargins(8, 6, 8, 6);

      QString const radius_path = QStringLiteral("maps/%1/chunk_manipulator/select_radius").arg(map_key);
      float const min_r = CHUNKSIZE * 0.25f;
      float const max_r = CHUNKSIZE * 8.0f;
      float const default_r = _tool ? _tool->selectRadius() : 80.f;
      float const saved_r = map_view->settings()->value(radius_path, default_r).toFloat();
      float const start_r = std::clamp(saved_r, min_r, max_r);

      auto* row = new QHBoxLayout();
      auto* lbl = new QLabel(tr("Radius"), selector);
      lbl->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
      row->addWidget(lbl);

      auto* spin = new QDoubleSpinBox(selector);
      spin->setRange(min_r, max_r);
      spin->setDecimals(2);
      spin->setSingleStep(CHUNKSIZE * 0.25f);
      spin->setValue(start_r);
      spin->setSuffix(tr(" yd"));
      row->addWidget(spin, 1);
      sel_l->addLayout(row);

      auto* slider = new QSlider(Qt::Horizontal, selector);
      slider->setRange(static_cast<int>(min_r * 10.f), static_cast<int>(max_r * 10.f));
      slider->setValue(static_cast<int>(start_r * 10.f));
      sel_l->addWidget(slider);

      auto apply_radius = [settings = map_view->settings(), radius_path, this](float r)
      {
        settings->setValue(radius_path, r);
        if (_tool)
          _tool->setSelectRadius(r);
      };

      QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), map_view, [=](double v)
      {
        float const r = static_cast<float>(v);
        slider->blockSignals(true);
        slider->setValue(static_cast<int>(r * 10.f));
        slider->blockSignals(false);
        apply_radius(r);
      });
      QObject::connect(slider, &QSlider::valueChanged, map_view, [=](int v)
      {
        float const r = static_cast<float>(v) / 10.f;
        spin->blockSignals(true);
        spin->setValue(r);
        spin->blockSignals(false);
        apply_radius(r);
      });

      // Apply on construction as well (keeps tool + settings in sync).
      if (_tool)
        _tool->setSelectRadius(start_r);

      QString const height_off_path = QStringLiteral("maps/%1/chunk_manipulator/paste_height_offset").arg(map_key);
      double const saved_dy = map_view->settings()->value(height_off_path, 0.0).toDouble();

      auto* row_dy = new QHBoxLayout();
      auto* lbl_dy = new QLabel(tr("Paste height offset"), selector);
      lbl_dy->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
      row_dy->addWidget(lbl_dy);

      auto* spin_dy = new QDoubleSpinBox(selector);
      spin_dy->setRange(-200000.0, 200000.0);
      spin_dy->setDecimals(2);
      spin_dy->setSingleStep(1.0);
      spin_dy->setValue(saved_dy);
      spin_dy->setSuffix(tr(" yd"));
      spin_dy->setToolTip(
        tr("Added to pasted terrain vertex heights, liquid surface heights, and pasted point-light Y. "
           "The paste preview overlay uses this value too."));
      row_dy->addWidget(spin_dy, 1);
      sel_l->addLayout(row_dy);

      QObject::connect(spin_dy, qOverload<double>(&QDoubleSpinBox::valueChanged), map_view,
        [settings = map_view->settings(), height_off_path, clip, map_view](double v)
        {
          settings->setValue(height_off_path, v);
          clip->setPasteTerrainHeightOffset(static_cast<float>(v));
          map_view->invalidate();
        });
      clip->setPasteTerrainHeightOffset(static_cast<float>(saved_dy));

      layout->addWidget(selector);
    }

    layout->addWidget(new ChunkGroupsWidget(clip, map_view->settings(), map_key, map_view, this));
    layout->addWidget(new ChunkCopyOptionsWidget(clip, map_view->settings(), map_key, this));

    auto* more = new QGroupBox(tr("Additional Features:"), this);
    auto* more_l = new QVBoxLayout(more);
    more_l->setContentsMargins(8, 6, 8, 6);
    auto* fix_gaps = new QCheckBox(tr("Fix Gaps"), more);
    QString const fix_path = QStringLiteral("maps/%1/chunk_manipulator/fix_gaps").arg(map_key);
    fix_gaps->setChecked(map_view->settings()->value(fix_path, true).toBool());
    QObject::connect(fix_gaps, &QCheckBox::toggled, map_view, [settings = map_view->settings(), fix_path](bool on)
    {
      settings->setValue(fix_path, on);
    });
    fix_gaps->setToolTip(
      tr("When enabled, after chunk paste (V) runs \"fix all gaps\" on loaded tiles: welds terrain heights at chunk and ADT edges. "
         "Neighboring tiles must be loaded for cross-ADT seams. This does not fill hole masks or liquid gaps."));
    more_l->addWidget(fix_gaps);

    auto* fix_now = new QPushButton(tr("Fix seams on loaded tiles…"), more);
    fix_now->setToolTip(
      tr("Runs the same seam weld as the node \"Loaded Tiles :: FixAllGaps\" on all currently loaded ADTs (one undo step)."));
    QObject::connect(fix_now, &QPushButton::clicked, map_view, [map_view]()
    {
      map_view->runFixAllTerrainGapsUndoable();
    });
    more_l->addWidget(fix_now);

    layout->addWidget(more);

    QObject::connect(clip, &ChunkClipboard::selectionChanged, map_view, [mv = map_view](auto const&) { mv->invalidate(); });
    QObject::connect(clip, &ChunkClipboard::selectionCleared, map_view, [mv = map_view]() { mv->invalidate(); });
    QObject::connect(clip, &ChunkClipboard::pasted, map_view, [mv = map_view]() { mv->invalidate(); });
  }
}
