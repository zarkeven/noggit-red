// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

namespace Noggit
{
  void RegisterErrorHandlers();
  void printStacktrace();

  /// Updates a short static string describing the current render path (main thread).
  /// Written before heavy GL work so crash logs can show where execution stopped.
  void register_crash_render_stage(char const* stage) noexcept;

  [[nodiscard]] char const* crash_render_stage() noexcept;
}
