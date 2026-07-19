// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include "ChunkManipulatorPanel.hpp"

#include "ChunkCopyOptionsWidget.hpp"
#include "ChunkGroupsWidget.hpp"

#include <noggit/MapView.h>
#include <noggit/World.h>

#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QSettings>
#include <QVBoxLayout>

using namespace Noggit::Ui::Tools::ChunkManipulator;

ChunkManipulatorPanel::ChunkManipulatorPanel(MapView* map_view, QWidget* parent)
  : QWidget(parent)
  , _map_view(map_view)
{
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(4, 4, 4, 4);
  layout->setSpacing(6);

  auto* title = new QLabel(tr("<b>Chunk Manipulator</b>"));
  title->setAlignment(Qt::AlignHCenter);
  title->setTextFormat(Qt::RichText);
  title->setToolTip(
    tr("Shift+LMB drag: add to quick selection. Ctrl+LMB drag: remove. Alt+RMB drag: brush radius.\n"
       "Ctrl+C: copy. V: paste at cursor (channels depend on Paste Allow).\n"
       "Right-click the group list to save the current selection or delete a group."));
  layout->addWidget(title);

  QString map_key = QString::fromStdString(map_view->getWorld()->basename);
  map_key.replace(QLatin1Char('/'), QLatin1Char('_'));

  if (auto* clip = map_view->getWorld()->chunkClipboard())
  {
    layout->addWidget(new ChunkGroupsWidget(clip, map_view->settings(), map_key, map_view, this));
    layout->addWidget(new ChunkCopyOptionsWidget(clip, map_view->settings(), map_key, this));

    auto* more = new QGroupBox(tr("Additional features:"), this);
    auto* more_l = new QVBoxLayout(more);
    more_l->setContentsMargins(8, 6, 8, 6);
    auto* fix_gaps = new QCheckBox(tr("Fix gaps"), more);
    QString const fix_path = QStringLiteral("maps/%1/chunk_manipulator/fix_gaps").arg(map_key);
    fix_gaps->setChecked(map_view->settings()->value(fix_path, true).toBool());
    QObject::connect(fix_gaps, &QCheckBox::toggled, map_view, [settings = map_view->settings(), fix_path](bool on)
    {
      settings->setValue(fix_path, on);
    });
    fix_gaps->setToolTip(tr("When implemented, will optionally fix terrain gaps after paste."));
    more_l->addWidget(fix_gaps);
    layout->addWidget(more);

    QObject::connect(clip, &ChunkClipboard::selectionChanged, map_view, [mv = map_view](auto const&) { mv->invalidate(); });
    QObject::connect(clip, &ChunkClipboard::selectionCleared, map_view, [mv = map_view]() { mv->invalidate(); });
    QObject::connect(clip, &ChunkClipboard::pasted, map_view, [mv = map_view]() { mv->invalidate(); });
  }
}
