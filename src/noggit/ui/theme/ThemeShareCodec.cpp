// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/ui/theme/ThemeShareCodec.hpp>

#include <QtCore/QByteArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Noggit::Ui::Theme
{
  QString ThemeShareCodec::compactPrefix()
  {
    return QStringLiteral("NOGGIT-THEME-1:");
  }

  QString ThemeShareCodec::encode(ThemePalette const& palette, ThemeShareFormat format)
  {
    QJsonDocument const doc(palette.toJson());
    QByteArray const json = doc.toJson(format == ThemeShareFormat::Json
                                         ? QJsonDocument::Indented
                                         : QJsonDocument::Compact);

    if (format == ThemeShareFormat::Json)
      return QString::fromUtf8(json);

    QByteArray const compressed = qCompress(json, 9);
    return compactPrefix() + QString::fromLatin1(compressed.toBase64());
  }

  std::optional<ThemePalette> ThemeShareCodec::decode(QString const& input, QString* error)
  {
    QString trimmed = input.trimmed();
    if (trimmed.isEmpty())
    {
      if (error)
        *error = QStringLiteral("Share code is empty.");
      return std::nullopt;
    }

    QByteArray json_bytes;

    if (trimmed.startsWith(compactPrefix()))
    {
      QByteArray const payload = trimmed.mid(compactPrefix().size()).trimmed().toLatin1();
      QByteArray const compressed = QByteArray::fromBase64(payload);
      if (compressed.isEmpty())
      {
        if (error)
          *error = QStringLiteral("Invalid compact theme code.");
        return std::nullopt;
      }

      json_bytes = qUncompress(compressed);
      if (json_bytes.isEmpty())
      {
        if (error)
          *error = QStringLiteral("Could not decompress theme code.");
        return std::nullopt;
      }
    }
    else
    {
      json_bytes = trimmed.toUtf8();
    }

    QJsonParseError parse_error;
    QJsonDocument const doc = QJsonDocument::fromJson(json_bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !doc.isObject())
    {
      if (error)
        *error = QStringLiteral("Theme data is not valid JSON.");
      return std::nullopt;
    }

    return ThemePalette::fromJson(doc.object(), error);
  }
}
