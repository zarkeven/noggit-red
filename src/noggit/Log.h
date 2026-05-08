// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <filesystem>
#include <iostream>

std::ostream& _LogError (const char* pFile, int pLine);
std::ostream& _LogDebug (const char* pFile, int pLine);
std::ostream& _Log (const char* pFile, int pLine);

#define LogError _LogError (__FILE__, __LINE__)
#define LogDebug _LogDebug (__FILE__, __LINE__)
#define Log _Log (__FILE__, __LINE__)

//! True when verbose load tracing is enabled: env \c NOGGIT_LOAD_TRACE (non-0/false/off), else QSettings
//! \a load_trace (default true) or legacy \a additional_file_loading_log.
[[nodiscard]] bool LoadTraceEnabled();

/// Opens `noggit.log` (truncates each run) and tees stdout/stderr/clog to the console and that file.
/// Tries in order: env \c NOGGIT_LOG_PATH (file path, or directory + \c noggit.log), then
/// \a log_directory / \c noggit.log, then cwd, then the system temp directory.
/// On Windows, the resolved path is also sent to \c OutputDebugString when opening succeeds.
/// If every open fails, logging stays console-only.
void InitLogging (std::filesystem::path const& log_directory = {});

/// Appends one line to \c noggit_missing_assets.txt next to the active \c noggit.log (or cwd if unset).
/// Thread-safe; used when async asset load fails so missing client data is easy to collect.
void LogMissingAsset (std::string const& asset_id, std::string const& detail);
