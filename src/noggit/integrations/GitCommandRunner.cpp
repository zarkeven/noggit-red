// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/integrations/GitCommandRunner.hpp>
#include <noggit/Log.h>

#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>

#include <system_error>

namespace Noggit::Integrations
{
  namespace
  {
    void applyAuthToEnvironment(QProcessEnvironment& env
                               , std::optional<GitAuthCredentials> const& auth)
    {
      if (!auth || auth->personal_access_token.isEmpty())
      {
        return;
      }

      QString const user = auth->username.isEmpty() ? QStringLiteral("git") : auth->username;
      QByteArray const basic = QByteArray(user.toUtf8() + ":" + auth->personal_access_token.toUtf8()).toBase64();
      env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
      env.insert(QStringLiteral("GIT_HTTP_EXTRAHEADER")
                , QStringLiteral("Authorization: Basic %1").arg(QString::fromLatin1(basic)));
    }
  }

  GitCommandResult GitCommandRunner::run(std::filesystem::path const& working_directory
                                        , QStringList const& arguments
                                        , std::optional<GitAuthCredentials> const& auth)
  {
    GitCommandResult result;

    QProcess process;
    process.setProgram(QStringLiteral("git"));
    process.setArguments(arguments);
    process.setWorkingDirectory(QString::fromStdString(working_directory.string()));

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    applyAuthToEnvironment(env, auth);
    process.setProcessEnvironment(env);

    process.start();
    if (!process.waitForStarted(15000))
    {
      result.standard_error = QStringLiteral("Failed to start git: %1").arg(process.errorString());
      LogError << result.standard_error.toStdString() << std::endl;
      return result;
    }

    if (!process.waitForFinished(120000))
    {
      process.kill();
      result.standard_error = QStringLiteral("git command timed out");
      LogError << result.standard_error.toStdString() << std::endl;
      return result;
    }

    result.exit_code = process.exitCode();
    result.standard_output = QString::fromUtf8(process.readAllStandardOutput());
    result.standard_error = QString::fromUtf8(process.readAllStandardError());
    result.success = (process.exitStatus() == QProcess::NormalExit && result.exit_code == 0);

    if (!result.success)
    {
      LogError << "git failed (" << result.exit_code << "): "
               << arguments.join(' ').toStdString()
               << " — " << result.standard_error.toStdString();
      if (!result.standard_output.isEmpty())
      {
        LogError << " | stdout: " << result.standard_output.toStdString();
      }
      LogError << std::endl;
    }

    return result;
  }

  bool GitCommandRunner::isGitRepository(std::filesystem::path const& path)
  {
    return gitRoot(path).has_value();
  }

  std::optional<std::string> GitCommandRunner::gitRoot(std::filesystem::path const& path)
  {
    auto const result = run(path, {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
    if (!result.success)
    {
      return std::nullopt;
    }

    QString const trimmed = result.standard_output.trimmed();
    if (trimmed.isEmpty())
    {
      return std::nullopt;
    }

    return trimmed.toStdString();
  }

  std::optional<std::string> GitCommandRunner::originRemoteUrl(std::filesystem::path const& path)
  {
    auto const result = run(path, {QStringLiteral("remote"), QStringLiteral("get-url"), QStringLiteral("origin")});
    if (!result.success)
    {
      return std::nullopt;
    }

    QString const trimmed = result.standard_output.trimmed();
    if (trimmed.isEmpty())
    {
      return std::nullopt;
    }

    return trimmed.toStdString();
  }

  GitCommandResult GitCommandRunner::pull(std::filesystem::path const& path
                                         , bool ff_only
                                         , std::optional<GitAuthCredentials> const& auth)
  {
    QStringList args {QStringLiteral("pull")};
    if (ff_only)
    {
      args << QStringLiteral("--ff-only");
    }
    return run(path, args, auth);
  }

  GitCommandResult GitCommandRunner::push(std::filesystem::path const& path
                                         , std::optional<GitAuthCredentials> const& auth)
  {
    return run(path, {QStringLiteral("push")}, auth);
  }

  GitCommandResult GitCommandRunner::add(std::filesystem::path const& path
                                        , QStringList const& paths)
  {
    QStringList args {QStringLiteral("add")};
    args << paths;
    return run(path, args);
  }

  GitCommandResult GitCommandRunner::commit(std::filesystem::path const& path
                                           , QString const& message)
  {
    return run(path, {QStringLiteral("commit"), QStringLiteral("-m"), message});
  }

  GitCommandResult GitCommandRunner::commit(std::filesystem::path const& path
                                           , QString const& message
                                           , QStringList const& pathspec)
  {
    QStringList args {QStringLiteral("commit"), QStringLiteral("-m"), message};
    if (!pathspec.isEmpty())
    {
      args << QStringLiteral("--");
      args << pathspec;
    }
    return run(path, args);
  }

  GitCommandResult GitCommandRunner::statusPorcelain(std::filesystem::path const& path)
  {
    return run(path, {QStringLiteral("status"), QStringLiteral("--porcelain")});
  }

  GitCommandResult GitCommandRunner::fetch(std::filesystem::path const& path
                                          , std::optional<GitAuthCredentials> const& auth)
  {
    return run(path, {QStringLiteral("fetch"), QStringLiteral("origin")}, auth);
  }

  std::optional<std::string> GitCommandRunner::upstreamRef(std::filesystem::path const& path)
  {
    auto const result = run(path, {QStringLiteral("rev-parse"), QStringLiteral("@{u}")});
    if (!result.success)
    {
      return std::nullopt;
    }

    QString const trimmed = result.standard_output.trimmed();
    if (trimmed.isEmpty())
    {
      return std::nullopt;
    }

    return trimmed.toStdString();
  }

  GitCommandResult GitCommandRunner::diffNameOnly(std::filesystem::path const& path
                                                 , QString const& rev_a
                                                 , QString const& rev_b)
  {
    return run(path, {QStringLiteral("diff"), QStringLiteral("--name-only"), rev_a + QStringLiteral("..") + rev_b});
  }

  GitCommandResult GitCommandRunner::resetHard(std::filesystem::path const& path
                                              , QString const& ref)
  {
    return run(path, {QStringLiteral("reset"), QStringLiteral("--hard"), ref});
  }
}
