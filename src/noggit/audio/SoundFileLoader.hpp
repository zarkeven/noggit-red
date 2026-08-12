// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <optional>
#include <string>

class QTemporaryFile;

namespace Noggit::Audio
{
  /// Extract a sound file from the WoW client archive to a temporary file on disk.
  /// The returned file is owned by the caller and must outlive playback.
  std::optional<QTemporaryFile*> extractSoundToTempFile(std::string const& filepath);
}
