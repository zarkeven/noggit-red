// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/project/VertexColorPalettePersistence.hpp>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QStringList>
#include <QTextStream>

#include <vector>

namespace
{
  struct MapPaletteEntry
  {
    QString map_name;
    QList<QColor> colors;
  };

  bool split_key_value(QString const& line, QString& out_key, QString& out_value)
  {
    int const eq = line.indexOf(QLatin1Char('='));
    if (eq < 0)
    {
      return false;
    }
    out_key = line.left(eq).trimmed();
    out_value = line.mid(eq + 1).trimmed();
    return !out_key.isEmpty();
  }

  QList<QColor> parse_colors_value(QString const& value)
  {
    QList<QColor> out;
    QString const normalized = value;
    for (QString const& piece : normalized.split(QLatin1Char(','), Qt::SkipEmptyParts))
    {
      QString t = piece.trimmed();
      if (t.startsWith(QLatin1Char('#')))
      {
        t = t.mid(1).trimmed();
      }
      if (t.size() != 6)
      {
        continue;
      }
      bool ok = false;
      unsigned const rgb = t.toUInt(&ok, 16);
      if (!ok || rgb > 0xFFFFFFu)
      {
        continue;
      }
      out.push_back(QColor::fromRgb(static_cast<int>((rgb >> 16) & 0xFFu),
                                    static_cast<int>((rgb >> 8) & 0xFFu),
                                    static_cast<int>(rgb & 0xFFu)));
    }
    return out;
  }

  QString format_rgb_hex_upper(QColor const& c)
  {
    return QStringLiteral("%1%2%3")
        .arg(c.red(), 2, 16, QLatin1Char('0'))
        .arg(c.green(), 2, 16, QLatin1Char('0'))
        .arg(c.blue(), 2, 16, QLatin1Char('0'))
        .toUpper();
  }

  [[nodiscard]] std::vector<MapPaletteEntry> read_all_entries(QString const& absolute_path)
  {
    std::vector<MapPaletteEntry> entries;
    QFile f(absolute_path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
      return entries;
    }

    QTextStream in(&f);
    QString pending_map;

    while (!in.atEnd())
    {
      QString const line_raw = in.readLine();
      QString const line = line_raw.trimmed();
      if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
      {
        continue;
      }

      QString key;
      QString value;
      if (!split_key_value(line, key, value))
      {
        continue;
      }

      if (key.compare(QStringLiteral("MapName"), Qt::CaseInsensitive) == 0)
      {
        pending_map = value;
        continue;
      }

      if (key.compare(QStringLiteral("Colors"), Qt::CaseInsensitive) == 0)
      {
        if (pending_map.isEmpty())
        {
          continue;
        }
        MapPaletteEntry e;
        e.map_name = pending_map;
        e.colors = parse_colors_value(value);
        entries.push_back(std::move(e));
        pending_map.clear();
      }
    }

    return entries;
  }

  void write_all_entries(QString const& absolute_path, std::vector<MapPaletteEntry> const& entries)
  {
    QFileInfo const fi(absolute_path);
    QDir().mkpath(fi.path());

    QFile f(absolute_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
      return;
    }

    QTextStream out(&f);
    out.setCodec("UTF-8");
    out << QStringLiteral("# Vertex color palettes per map (saved by Noggit)\n");
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
      MapPaletteEntry const& e = entries[i];
      out << QStringLiteral("MapName = ") << e.map_name << QLatin1Char('\n');
      QStringList hexes;
      hexes.reserve(e.colors.size());
      for (QColor const& c : e.colors)
      {
        if (!c.isValid())
        {
          continue;
        }
        hexes.push_back(format_rgb_hex_upper(c));
      }
      out << QStringLiteral("Colors = ") << hexes.join(QStringLiteral(", ")) << QLatin1Char('\n');
      if (i + 1 < entries.size())
      {
        out << QLatin1Char('\n');
      }
    }
  }
}

namespace Noggit::Project
{
  QString vertexColorPalettesFileName() noexcept
  {
    return QStringLiteral("vertex_color_palettes.txt");
  }

  QString vertexColorPalettesAbsolutePath(QString const& project_dir)
  {
    QString const trimmed = project_dir.trimmed();
    if (trimmed.isEmpty())
    {
      return {};
    }
    return QDir(trimmed).filePath(vertexColorPalettesFileName());
  }

  QList<QColor> loadVertexColorPaletteForMap(QString const& project_dir, QString const& map_basename)
  {
    QString const path = vertexColorPalettesAbsolutePath(project_dir);
    if (path.isEmpty() || map_basename.isEmpty())
    {
      return {};
    }

    std::vector<MapPaletteEntry> const entries = read_all_entries(path);
    for (MapPaletteEntry const& e : entries)
    {
      if (e.map_name == map_basename)
      {
        return e.colors;
      }
    }
    return {};
  }

  void saveVertexColorPaletteForMap(QString const& project_dir,
                                    QString const& map_basename,
                                    QList<QColor> const& colors)
  {
    QString const path = vertexColorPalettesAbsolutePath(project_dir);
    if (path.isEmpty() || map_basename.isEmpty())
    {
      return;
    }

    std::vector<MapPaletteEntry> entries = read_all_entries(path);

    bool replaced = false;
    for (MapPaletteEntry& e : entries)
    {
      if (e.map_name == map_basename)
      {
        e.colors = colors;
        replaced = true;
        break;
      }
    }
    if (!replaced)
    {
      MapPaletteEntry e;
      e.map_name = map_basename;
      e.colors = colors;
      entries.push_back(std::move(e));
    }

    write_all_entries(path, entries);
  }
}
