// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <noggit/ui/theme/ThemePalette.hpp>

#include <QtCore/QString>

#include <optional>

namespace Noggit::Ui::Theme
{
  enum class ThemeShareFormat
  {
    Compact,
    Json,
  };

  struct ThemeShareCodec
  {
    static QString encode(ThemePalette const& palette, ThemeShareFormat format);
    static std::optional<ThemePalette> decode(QString const& input, QString* error = nullptr);

    static QString compactPrefix();
  };
}
