// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/Log.h>

#include <QtCore/QSettings>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <streambuf>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#endif

namespace
{
  [[nodiscard]] char const* log_basename (char const* pFile)
  {
    char const* const slash = std::strrchr (pFile, '/');
    char const* const bslash = std::strrchr (pFile, '\\');
    char const* const base = (std::max) (slash ? slash + 1 : pFile, bslash ? bslash + 1 : pFile);
    return base;
  }

  std::ofstream g_log_file;
  std::filesystem::path g_log_path_opened;

  std::streambuf* g_saved_cout = nullptr;
  std::streambuf* g_saved_clog = nullptr;
  std::streambuf* g_saved_cerr = nullptr;

  /// Serialize all writes: AsyncLoader threads + main both log; sharing one filebuf without a lock
  /// can yield empty/corrupt logs on Windows. Also write the file before the console so GUI apps
  /// (no usable stdout) still persist every line.
  std::mutex g_log_tee_mutex;

  /// Duplicate output to two streambufs (console + log file).
  class tee_buf final : public std::streambuf
  {
  public:
    tee_buf (std::streambuf* primary, std::streambuf* secondary)
      : _primary (primary)
      , _secondary (secondary)
    {}

  protected:
    int_type overflow (int_type c) override
    {
      if (traits_type::eq_int_type (c, traits_type::eof()))
      {
        return sync() == 0 ? traits_type::not_eof (0) : traits_type::eof();
      }

      char const ch = traits_type::to_char_type (c);
      std::lock_guard<std::mutex> const lock (g_log_tee_mutex);

      if (traits_type::eq_int_type (_secondary->sputc (ch), traits_type::eof()))
      {
        return traits_type::eof();
      }
      (void)_primary->sputc (ch);
      return traits_type::not_eof (c);
    }

    int sync() override
    {
      std::lock_guard<std::mutex> const lock (g_log_tee_mutex);
      int const a = _primary->pubsync();
      int const b = _secondary->pubsync();
      if (g_log_file.is_open())
      {
        g_log_file.flush();
      }
      return (a == 0 && b == 0) ? 0 : -1;
    }

    std::streamsize xsputn (char const* s, std::streamsize n) override
    {
      if (n == 0)
      {
        return 0;
      }
      std::lock_guard<std::mutex> const lock (g_log_tee_mutex);
      std::streamsize const n2 = _secondary->sputn (s, n);
      if (n2 != n)
      {
        return -1;
      }
      (void)_primary->sputn (s, n);
      if (g_log_file.is_open())
      {
        g_log_file.flush();
      }
      return n;
    }

  private:
    std::streambuf* _primary;
    std::streambuf* _secondary;
  };

  std::unique_ptr<tee_buf> g_tee_cout;
  std::unique_ptr<tee_buf> g_tee_clog;
  std::unique_ptr<tee_buf> g_tee_cerr;
}

std::ostream& _LogError (const char* pFile, int pLine)
{
  return std::cerr << clock() * 1000 / CLOCKS_PER_SEC << " - (" << log_basename (pFile) << ":" << pLine
                   << "): [Error] ";
}
std::ostream& _LogDebug (const char* pFile, int pLine)
{
  return std::cout << clock() * 1000 / CLOCKS_PER_SEC << " - (" << log_basename (pFile) << ":" << pLine
                   << "): [Debug] ";
}

bool LoadTraceEnabled()
{
  if (char const* const env = std::getenv ("NOGGIT_LOAD_TRACE"))
  {
    std::string_view const v (env);
    if (v == "0" || v == "false" || v == "off" || v == "OFF")
    {
      return false;
    }
    return true;
  }

  QSettings const settings;
  return settings.value ("load_trace", true).toBool()
      || settings.value ("additional_file_loading_log", false).toBool();
}

std::ostream& _Log (const char* pFile, int pLine)
{
  return std::cout << clock() * 1000 / CLOCKS_PER_SEC << " - (" << log_basename (pFile) << ":" << pLine << "): ";
}

void InitLogging (std::filesystem::path const& log_directory)
{
  std::filesystem::path const dir =
      log_directory.empty() ? std::filesystem::current_path() : log_directory;

  std::vector<std::filesystem::path> candidates;
  if (char const* const env = std::getenv ("NOGGIT_LOG_PATH"))
  {
    std::filesystem::path const p (env);
    if (!p.empty())
    {
      std::error_code ec;
      if (std::filesystem::is_directory (p, ec))
      {
        candidates.push_back (p / "noggit.log");
      }
      else
      {
        candidates.push_back (p);
      }
    }
  }
  candidates.push_back (dir / "noggit.log");
  candidates.push_back (std::filesystem::current_path() / "noggit.log");
  candidates.push_back (std::filesystem::temp_directory_path() / "noggit.log");

  g_log_path_opened.clear();
  for (auto const& log_path : candidates)
  {
    std::error_code ec;
    std::filesystem::create_directories (log_path.parent_path(), ec);
    g_log_file.open (log_path, std::ios_base::out | std::ios_base::trunc);
    if (g_log_file)
    {
      g_log_path_opened = log_path;
      break;
    }
  }

  if (!g_log_file)
  {
#if defined(_WIN32)
    OutputDebugStringA ("Noggit: InitLogging failed to open any noggit.log candidate path.\n");
#endif
    return;
  }

  g_saved_cout = std::cout.rdbuf();
  g_saved_clog = std::clog.rdbuf();
  g_saved_cerr = std::cerr.rdbuf();

  g_tee_cout = std::make_unique<tee_buf> (g_saved_cout, g_log_file.rdbuf());
  g_tee_clog = std::make_unique<tee_buf> (g_saved_clog, g_log_file.rdbuf());
  g_tee_cerr = std::make_unique<tee_buf> (g_saved_cerr, g_log_file.rdbuf());

  std::cout.rdbuf (g_tee_cout.get());
  std::clog.rdbuf (g_tee_clog.get());
  std::cerr.rdbuf (g_tee_cerr.get());

  g_log_file << std::unitbuf;
  std::cout << std::unitbuf;
  std::clog << std::unitbuf;
  std::cerr << std::unitbuf;

  std::time_t const now = std::time (nullptr);
  char timebuf[64]{};
#if defined(_MSC_VER)
  std::tm tm_now{};
  localtime_s (&tm_now, &now);
  std::strftime (timebuf, sizeof (timebuf), "%Y-%m-%d %H:%M:%S", &tm_now);
#else
  if (std::tm* tm_now = std::localtime (&now))
  {
    std::strftime (timebuf, sizeof (timebuf), "%Y-%m-%d %H:%M:%S", tm_now);
  }
#endif

  // Prove the file is writable even if the GUI build has no real stdout.
  g_log_file << clock() * 1000 / CLOCKS_PER_SEC << " - (Log.cpp:" << __LINE__ << "): Log file: "
             << g_log_path_opened.generic_string() << "\n";
  g_log_file.flush();

#if defined(_WIN32)
  {
    std::string const msg = std::string ("Noggit: logging to ") + g_log_path_opened.string() + "\n";
    OutputDebugStringA (msg.c_str());
  }
#endif

  std::cout << "\n" << clock() * 1000 / CLOCKS_PER_SEC << " - (Log.cpp:" << __LINE__ << "): Session " << timebuf
             << " | " << g_log_path_opened.generic_string() << std::endl;
}

namespace
{
  std::mutex g_missing_asset_mutex;

  [[nodiscard]] std::filesystem::path missing_asset_log_path()
  {
    if (!g_log_path_opened.empty())
    {
      return g_log_path_opened.parent_path() / "noggit_missing_assets.txt";
    }
    if (char const* const env = std::getenv ("NOGGIT_LOG_PATH"))
    {
      std::filesystem::path const p (env);
      if (!p.empty())
      {
        std::error_code ec;
        if (std::filesystem::is_directory (p, ec))
        {
          return p / "noggit_missing_assets.txt";
        }
        if (p.has_parent_path())
        {
          return p.parent_path() / "noggit_missing_assets.txt";
        }
      }
    }
    return std::filesystem::current_path() / "noggit_missing_assets.txt";
  }

  void sanitize_one_line (std::string& s)
  {
    for (char& c : s)
    {
      if (c == '\n' || c == '\r')
      {
        c = ' ';
      }
    }
  }
}

void LogMissingAsset (std::string const& asset_id, std::string const& detail)
{
  std::string id_copy = asset_id;
  std::string detail_copy = detail;
  sanitize_one_line (id_copy);
  sanitize_one_line (detail_copy);

  std::lock_guard<std::mutex> const lock (g_missing_asset_mutex);

  std::filesystem::path const path = missing_asset_log_path();
  std::error_code ec;
  std::filesystem::create_directories (path.parent_path(), ec);

  std::ofstream f (path, std::ios::app);
  if (!f)
  {
    return;
  }

  std::time_t const now = std::time (nullptr);
  char timebuf[64]{};
#if defined(_MSC_VER)
  std::tm tm_now{};
  localtime_s (&tm_now, &now);
  std::strftime (timebuf, sizeof (timebuf), "%Y-%m-%d %H:%M:%S", &tm_now);
#else
  if (std::tm* tm_now = std::localtime (&now))
  {
    std::strftime (timebuf, sizeof (timebuf), "%Y-%m-%d %H:%M:%S", tm_now);
  }
#endif

  f << timebuf << " | " << id_copy << " | " << detail_copy << '\n';
  f.flush();
}
