// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <QString>
#include <QList>

class QColor;

namespace Noggit::Project
{
  //! File stored in the project root: one or more blocks of MapName / Colors lines.
  [[nodiscard]] QString vertexColorPalettesFileName() noexcept;

  [[nodiscard]] QString vertexColorPalettesAbsolutePath(QString const& project_dir);

  [[nodiscard]] QList<QColor> loadVertexColorPaletteForMap(QString const& project_dir,
                                                          QString const& map_basename);

  void saveVertexColorPaletteForMap(QString const& project_dir,
                                    QString const& map_basename,
                                    QList<QColor> const& colors);
}
