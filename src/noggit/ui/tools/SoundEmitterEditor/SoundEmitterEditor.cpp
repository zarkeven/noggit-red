// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include "SoundEmitterEditor.hpp"

#include <noggit/ActionManager.hpp>
#include <noggit/DBC.h>
#include <noggit/MapChunk.h>
#include <noggit/MapHeaders.h>
#include <noggit/MapView.h>
#include <noggit/World.h>
#include <noggit/ui/windows/EditorWindows/SoundEntryPickerWindow.h>
#include <noggit/ui/windows/SoundPlayer/SoundEntryPlayer.h>

#include <QAbstractItemView>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

using namespace Noggit::Ui::Tools;

namespace
{
  void setEmitterSize(ENTRY_MCSE& e, glm::vec3 const& s)
  {
    e.size[0] = s.x;
    e.size[1] = s.y;
    e.size[2] = s.z;
  }

  glm::vec3 defaultEmitterSizeForAdvancedId(std::uint32_t advanced_id)
  {
    float radius = 10.f;
    try
    {
      SoundEntriesAdvancedDB::Record const advanced = gSoundEntriesAdvancedDB.getByID(advanced_id);
      radius = std::max(advanced.getFloat(SoundEntriesAdvancedDB::outerRadiusOfInfluence), 1.f);
    }
    catch (SoundEntriesAdvancedDB::NotFound)
    {
    }
    return { radius, radius, radius };
  }

  QString soundEntryLabel(std::uint32_t mcse_sound_id)
  {
    if (mcse_sound_id == 0)
      return QStringLiteral("(none)");

    if (auto const entry_id = resolveSoundEntryId(mcse_sound_id))
    {
      try
      {
        SoundEntriesDB::Record const rec = gSoundEntriesDB.getByID(*entry_id);
        return QStringLiteral("%1 — %2").arg(*entry_id).arg(rec.getString(SoundEntriesDB::Name));
      }
      catch (SoundEntriesDB::NotFound)
      {
        return QString::number(*entry_id);
      }
    }

    return QStringLiteral("advanced %1").arg(mcse_sound_id);
  }
}

SoundEmitterEditor::SoundEmitterEditor(::MapView* mapView, QWidget* parent)
  : QWidget(parent)
  , _mapView(mapView)
{
  setMinimumWidth(250);

  auto* root_layout = new QVBoxLayout(this);
  root_layout->setContentsMargins(0, 0, 0, 0);
  root_layout->setSpacing(8);

  auto* panel_box = new QGroupBox(tr("Sound emitters (MCSE)"), this);
  panel_box->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
  root_layout->addWidget(panel_box, 1);

  auto* panel_layout = new QVBoxLayout(panel_box);
  panel_layout->setContentsMargins(6, 6, 6, 6);
  panel_layout->setSpacing(6);

  panel_layout->addWidget(new QLabel(tr("Emitters in chunk under cursor:"), panel_box));

  _emitterList = new QListWidget(panel_box);
  _emitterList->setSelectionMode(QAbstractItemView::SingleSelection);
  panel_layout->addWidget(_emitterList, 1);

  auto* sound_id_row = new QHBoxLayout();
  auto* advanced_id_spin = new QSpinBox(panel_box);
  advanced_id_spin->setRange(0, 2147483647);
  advanced_id_spin->setToolTip(tr("SoundEntriesAdvanced ID stored in MCSE soundId."));
  auto* pick_sound_btn = new QPushButton(tr("Pick Sound…"), panel_box);
  auto* preview_sound_btn = new QPushButton(tr("Preview"), panel_box);
  sound_id_row->addWidget(new QLabel(tr("Advanced ID:"), panel_box));
  sound_id_row->addWidget(advanced_id_spin, 1);
  sound_id_row->addWidget(pick_sound_btn);
  sound_id_row->addWidget(preview_sound_btn);
  panel_layout->addLayout(sound_id_row);

  auto* resolved_lbl = new QLabel(panel_box);
  resolved_lbl->setWordWrap(true);
  panel_layout->addWidget(resolved_lbl);

  auto make_vec3_row = [&](QString const& title, QDoubleSpinBox*& out_x, QDoubleSpinBox*& out_y, QDoubleSpinBox*& out_z)
  {
    panel_layout->addWidget(new QLabel(title, panel_box));
    auto* row = new QHBoxLayout();
    out_x = new QDoubleSpinBox(panel_box);
    out_y = new QDoubleSpinBox(panel_box);
    out_z = new QDoubleSpinBox(panel_box);
    for (auto* spin : { out_x, out_y, out_z })
    {
      spin->setRange(-100000.0, 100000.0);
      spin->setDecimals(2);
      spin->setSingleStep(0.5);
      row->addWidget(spin);
    }
    panel_layout->addLayout(row);
  };

  QDoubleSpinBox *pos_x = nullptr, *pos_y = nullptr, *pos_z = nullptr;
  QDoubleSpinBox *size_x = nullptr, *size_y = nullptr, *size_z = nullptr;
  make_vec3_row(tr("Position"), pos_x, pos_y, pos_z);
  make_vec3_row(tr("Influence size"), size_x, size_y, size_z);

  auto* buttons_row = new QWidget(panel_box);
  auto* buttons_layout = new QHBoxLayout(buttons_row);
  buttons_layout->setContentsMargins(0, 0, 0, 0);
  auto* add_btn = new QPushButton(tr("Add"), buttons_row);
  auto* dup_btn = new QPushButton(tr("Duplicate"), buttons_row);
  auto* del_btn = new QPushButton(tr("Delete"), buttons_row);
  buttons_layout->addWidget(add_btn);
  buttons_layout->addWidget(dup_btn);
  buttons_layout->addWidget(del_btn);
  panel_layout->addWidget(buttons_row);

  auto chunk_under_cursor = [this]() -> MapChunk*
  {
    if (_mapView->displayMode() != display_mode::in_3D)
      return nullptr;
    glm::vec3 ref = _mapView->cursorPosition();
    if (auto const hit = _mapView->pickTerrainWorldPositionUnderCursor())
      ref = *hit;
    return _mapView->getWorld()->getChunkAt(ref);
  };

  auto apply_to_selected = [this](std::function<void(ENTRY_MCSE&)> mut)
  {
    ENTRY_MCSE* entry = _mapView->getWorld()->getSelectedSoundEmitterEntry();
    if (!entry)
      return;
    auto const sel = *_mapView->getWorld()->selectedSoundEmitter();
    _mapView->touchSoundEmitterPropertyUndoBatch();
    if (NOGGIT_CUR_ACTION)
      NOGGIT_CUR_ACTION->registerChunkSoundEmitterChange(sel.chunk);
    mut(*entry);
    _mapView->getWorld()->markSoundEmitterChunkDirty(sel.chunk);
    _mapView->invalidate();
  };

  auto refresh_list = [this, chunk_under_cursor]()
  {
    _listed_chunk = chunk_under_cursor();

    QSignalBlocker const _block(_emitterList);
    _emitterList->clear();

    if (!_listed_chunk)
      return;

    auto const& emitters = _listed_chunk->sound_emitters;
    for (std::size_t i = 0; i < emitters.size(); ++i)
    {
      auto const& e = emitters[i];
      _emitterList->addItem(QStringLiteral("#%1 — %2")
                              .arg(i + 1)
                              .arg(soundEntryLabel(e.soundId)));
    }

    if (auto const sel = _mapView->getWorld()->selectedSoundEmitter();
        sel && sel->chunk == _listed_chunk && sel->index < emitters.size())
    {
      _emitterList->setCurrentRow(static_cast<int>(sel->index));
    }
  };

  auto load_selected_into_ui = [this, advanced_id_spin, resolved_lbl, pos_x, pos_y, pos_z, size_x, size_y, size_z]()
  {
    ENTRY_MCSE const* entry = _mapView->getWorld()->getSelectedSoundEmitterEntry();
    if (!entry)
      return;

    {
      QSignalBlocker const _ba(advanced_id_spin);
      QSignalBlocker const _bp(pos_x);
      QSignalBlocker const _by(pos_y);
      QSignalBlocker const _bz(pos_z);
      QSignalBlocker const _bsx(size_x);
      QSignalBlocker const _bsy(size_y);
      QSignalBlocker const _bsz(size_z);

      advanced_id_spin->setValue(static_cast<int>(entry->soundId));
      pos_x->setValue(entry->pos[0]);
      pos_y->setValue(entry->pos[1]);
      pos_z->setValue(entry->pos[2]);
      size_x->setValue(entry->size[0]);
      size_y->setValue(entry->size[1]);
      size_z->setValue(entry->size[2]);
    }

    resolved_lbl->setText(tr("Resolved: %1").arg(soundEntryLabel(entry->soundId)));
  };

  QObject::connect(_emitterList, &QListWidget::currentRowChanged, [this, load_selected_into_ui](int row)
  {
    _mapView->flushSoundEmitterPropertyUndoBatch();
    if (row < 0 || !_listed_chunk)
      return;

    if (static_cast<std::size_t>(row) >= _listed_chunk->sound_emitters.size())
      return;

    _mapView->getWorld()->setSelectedSoundEmitter(SoundEmitterRef{ _listed_chunk, static_cast<std::size_t>(row) });
    load_selected_into_ui();
    _mapView->invalidate();
  });

  QObject::connect(add_btn, &QPushButton::clicked, [this, refresh_list, load_selected_into_ui, chunk_under_cursor]()
  {
    MapChunk* chunk = chunk_under_cursor();
    if (!chunk)
    {
      QMessageBox::warning(this, tr("Sound emitters"), tr("Switch to 3D mode and place the cursor over a loaded chunk."));
      return;
    }

    _mapView->recordSoundEmitterChange([this, chunk, refresh_list, load_selected_into_ui]()
    {
      glm::vec3 spawn = _mapView->cursorPosition();
      if (auto const hit = _mapView->pickTerrainWorldPositionUnderCursor())
        spawn = *hit;

      ENTRY_MCSE emitter{};
      emitter.soundId = 0;
      emitter.pos[0] = spawn.x;
      emitter.pos[1] = spawn.y;
      emitter.pos[2] = spawn.z;
      setEmitterSize(emitter, { 10.f, 10.f, 10.f });

      chunk->sound_emitters.push_back(emitter);
      _mapView->getWorld()->markSoundEmitterChunkDirty(chunk);

      _mapView->getWorld()->setSelectedSoundEmitter(SoundEmitterRef{ chunk, chunk->sound_emitters.size() - 1 });
      refresh_list();
      load_selected_into_ui();
      _mapView->invalidate();
    });
  });

  _deleteSelected = [this, refresh_list]()
  {
    auto const sel = _mapView->getWorld()->selectedSoundEmitter();
    if (!sel)
      return;

    MapChunk* chunk = sel->chunk;
    if (!chunk || sel->index >= chunk->sound_emitters.size())
      return;

    _mapView->recordSoundEmitterChange([this, chunk, idx = sel->index, refresh_list]()
    {
      chunk->sound_emitters.erase(chunk->sound_emitters.begin() + static_cast<std::ptrdiff_t>(idx));
      _mapView->getWorld()->markSoundEmitterChunkDirty(chunk);
      _mapView->getWorld()->clearSelectedSoundEmitter();
      refresh_list();
      {
        QSignalBlocker const _b(_emitterList);
        _emitterList->setCurrentRow(-1);
      }
      _mapView->invalidate();
    });
  };

  QObject::connect(del_btn, &QPushButton::clicked, [this]() {
    if (_deleteSelected)
      _deleteSelected();
  });

  QObject::connect(dup_btn, &QPushButton::clicked, [this, refresh_list, load_selected_into_ui]()
  {
    auto const sel = _mapView->getWorld()->selectedSoundEmitter();
    if (!sel || !sel->chunk || sel->index >= sel->chunk->sound_emitters.size())
      return;

    _mapView->recordSoundEmitterChange([this, sel = *sel, refresh_list, load_selected_into_ui]()
    {
      MapChunk* chunk = sel.chunk;
      ENTRY_MCSE copy = chunk->sound_emitters[sel.index];
      copy.pos[0] += 2.f;
      chunk->sound_emitters.push_back(copy);
      _mapView->getWorld()->markSoundEmitterChunkDirty(chunk);
      _mapView->getWorld()->setSelectedSoundEmitter(SoundEmitterRef{ chunk, chunk->sound_emitters.size() - 1 });
      refresh_list();
      load_selected_into_ui();
      _mapView->invalidate();
    });
  });

  QObject::connect(advanced_id_spin, QOverload<int>::of(&QSpinBox::valueChanged), [this, apply_to_selected, resolved_lbl, size_x, size_y, size_z](int v)
  {
    apply_to_selected([=](ENTRY_MCSE& e) {
      e.soundId = static_cast<std::uint32_t>(v);
      if (e.size[0] <= 0.f && e.size[1] <= 0.f && e.size[2] <= 0.f)
      {
        glm::vec3 const s = defaultEmitterSizeForAdvancedId(e.soundId);
        setEmitterSize(e, s);
        QSignalBlocker const _sx(size_x);
        QSignalBlocker const _sy(size_y);
        QSignalBlocker const _sz(size_z);
        size_x->setValue(s.x);
        size_y->setValue(s.y);
        size_z->setValue(s.z);
      }
    });
    if (ENTRY_MCSE const* entry = _mapView->getWorld()->getSelectedSoundEmitterEntry())
      resolved_lbl->setText(tr("Resolved: %1").arg(soundEntryLabel(entry->soundId)));
  });

  auto bind_double = [apply_to_selected](QDoubleSpinBox* box, std::function<void(ENTRY_MCSE&, double)> apply)
  {
    QObject::connect(box, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [apply_to_selected, apply](double v)
    {
      apply_to_selected([apply, v](ENTRY_MCSE& e) { apply(e, v); });
    });
  };

  bind_double(pos_x, [](ENTRY_MCSE& e, double v) { e.pos[0] = static_cast<float>(v); });
  bind_double(pos_y, [](ENTRY_MCSE& e, double v) { e.pos[1] = static_cast<float>(v); });
  bind_double(pos_z, [](ENTRY_MCSE& e, double v) { e.pos[2] = static_cast<float>(v); });
  bind_double(size_x, [](ENTRY_MCSE& e, double v) { e.size[0] = static_cast<float>(v); });
  bind_double(size_y, [](ENTRY_MCSE& e, double v) { e.size[1] = static_cast<float>(v); });
  bind_double(size_z, [](ENTRY_MCSE& e, double v) { e.size[2] = static_cast<float>(v); });

  QObject::connect(pick_sound_btn, &QPushButton::clicked, [this, pick_sound_btn, advanced_id_spin, resolved_lbl, size_x, size_y, size_z, apply_to_selected]()
  {
    auto* picker_btn = new QPushButton(this);
    picker_btn->setProperty("id", advanced_id_spin->value());
    picker_btn->hide();

    auto* picker = new Noggit::Ui::SoundEntryPickerWindow(picker_btn, Noggit::Ui::TERRAIN_EMITER, false, this);
    picker->setAttribute(Qt::WA_DeleteOnClose);
    picker->show();

    QObject::connect(picker, &QObject::destroyed, this, [this, picker_btn, advanced_id_spin, resolved_lbl, size_x, size_y, size_z, apply_to_selected]()
    {
      int const sound_entry_id = picker_btn->property("id").toInt();
      picker_btn->deleteLater();
      if (sound_entry_id <= 0)
        return;

      auto const advanced_id = findSoundEntriesAdvancedId(static_cast<std::uint32_t>(sound_entry_id));
      if (!advanced_id)
      {
        QMessageBox::warning(this, tr("Sound emitters"),
                             tr("No SoundEntriesAdvanced row links to SoundEntries id %1.\n"
                                "Set the Advanced ID manually or pick a different sound.").arg(sound_entry_id));
        return;
      }

      apply_to_selected([=](ENTRY_MCSE& e) {
        e.soundId = *advanced_id;
        glm::vec3 const s = defaultEmitterSizeForAdvancedId(*advanced_id);
        setEmitterSize(e, s);
        QSignalBlocker const _id(advanced_id_spin);
        advanced_id_spin->setValue(static_cast<int>(*advanced_id));
        QSignalBlocker const _sx(size_x);
        QSignalBlocker const _sy(size_y);
        QSignalBlocker const _sz(size_z);
        size_x->setValue(s.x);
        size_y->setValue(s.y);
        size_z->setValue(s.z);
      });

      resolved_lbl->setText(tr("Resolved: %1").arg(soundEntryLabel(*advanced_id)));
    });
  });

  QObject::connect(preview_sound_btn, &QPushButton::clicked, [this]()
  {
    ENTRY_MCSE const* entry = _mapView->getWorld()->getSelectedSoundEmitterEntry();
    if (!entry)
      return;

    auto const entry_id = resolveSoundEntryId(entry->soundId);
    if (!entry_id)
    {
      QMessageBox::information(this, tr("Sound emitters"), tr("No SoundEntries row resolved for this emitter."));
      return;
    }

    auto* player = new Noggit::Ui::SoundEntryPlayer(this);
    player->LoadSoundsFromSoundEntry(static_cast<int>(*entry_id));
    player->show();
  });

  _refresh_emitter_list = refresh_list;
  _sync_selected_emitter_to_ui = load_selected_into_ui;

  refresh_list();
  load_selected_into_ui();
}

void SoundEmitterEditor::keyPressEvent(QKeyEvent* event)
{
  if (event && event->key() == Qt::Key_Delete)
  {
    if (_deleteSelected)
      _deleteSelected();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void SoundEmitterEditor::refreshFromWorld()
{
  if (!_mapView || !_emitterList)
    return;

  if (_refresh_emitter_list)
    _refresh_emitter_list();
  if (_sync_selected_emitter_to_ui)
    _sync_selected_emitter_to_ui();
}
