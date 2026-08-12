// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <QString>
#include <QStringList>

namespace Noggit::Integrations
{
  struct GitCommandResult
  {
    bool success = false;
    int exit_code = -1;
    QString standard_output;
    QString standard_error;
  };

  struct GitAuthCredentials
  {
    QString username;
    QString personal_access_token;
  };

  class GitCommandRunner
  {
  public:
    static GitCommandResult run(std::filesystem::path const& working_directory
                               , QStringList const& arguments
                               , std::optional<GitAuthCredentials> const& auth = std::nullopt);

    static bool isGitRepository(std::filesystem::path const& path);
    static std::optional<std::string> gitRoot(std::filesystem::path const& path);
    static std::optional<std::string> originRemoteUrl(std::filesystem::path const& path);

    static GitCommandResult pull(std::filesystem::path const& path
                                , bool ff_only
                                , std::optional<GitAuthCredentials> const& auth = std::nullopt);

    static GitCommandResult push(std::filesystem::path const& path
                                , std::optional<GitAuthCredentials> const& auth = std::nullopt);

    static GitCommandResult add(std::filesystem::path const& path
                               , QStringList const& paths);

    static GitCommandResult commit(std::filesystem::path const& path
                                  , QString const& message);

    static GitCommandResult commit(std::filesystem::path const& path
                                  , QString const& message
                                  , QStringList const& pathspec);

    static GitCommandResult statusPorcelain(std::filesystem::path const& path);

    static GitCommandResult fetch(std::filesystem::path const& path
                                 , std::optional<GitAuthCredentials> const& auth = std::nullopt);

    static std::optional<std::string> upstreamRef(std::filesystem::path const& path);

    static GitCommandResult diffNameOnly(std::filesystem::path const& path
                                        , QString const& rev_a
                                        , QString const& rev_b);

    static GitCommandResult resetHard(std::filesystem::path const& path
                                     , QString const& ref);
  };
}
