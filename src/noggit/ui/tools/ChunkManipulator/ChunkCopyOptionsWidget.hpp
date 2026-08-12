// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/ui/tools/ChunkManipulator/ChunkClipboard.hpp>

#include <QString>
#include <QWidget>

class QCheckBox;
class QSettings;

namespace Noggit::Ui::Tools::ChunkManipulator
{
  /// Compact "Paste allow" grid: toggles map to `ChunkClipboard::setCopyParams`.
  /// Textures and area ID are persisted but not shown (same bits as last save / defaults).
  class ChunkCopyOptionsWidget final : public QWidget
  {
    Q_OBJECT

  public:
    ChunkCopyOptionsWidget(ChunkClipboard* clipboard, QSettings* settings, QString map_basename_key, QWidget* parent = nullptr);

    [[nodiscard]] ChunkCopyFlags flagsFromUi() const;
    void setUiFromFlags(ChunkCopyFlags flags);

  private slots:
    void onAnyToggled();

  private:
    static constexpr unsigned extra_copy_bits_mask() noexcept
    {
      return to_underlying(ChunkCopyFlags::TEXTURES) | to_underlying(ChunkCopyFlags::AREA_ID);
    }

    void persist() const;
    void readPersisted();
    void pushFlagsToClipboard();

    ChunkClipboard* _clipboard;
    QSettings* _settings;
    QString const _map_key;

    /// Bits for TEXTURES and AREA_ID kept in sync with clipboard / QSettings when those channels have no checkbox.
    unsigned _extra_copy_bits = extra_copy_bits_mask();

    QCheckBox* _models = nullptr;
    QCheckBox* _terrain = nullptr;
    QCheckBox* _liquid = nullptr;
    QCheckBox* _flags = nullptr;
    QCheckBox* _wmos = nullptr;
    QCheckBox* _holes = nullptr;
    QCheckBox* _vertex_colors = nullptr;
    QCheckBox* _shadows = nullptr;
  };
}
