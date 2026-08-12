// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/project/WowExportListfileDownload.hpp>

#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <cctype>
#include <string>

namespace Noggit::Project
{
  namespace
  {
    bool body_looks_like_listfile_csv (QByteArray const& data, QString* error_message)
    {
      if (data.size() < 64)
      {
        if (error_message)
          *error_message = QStringLiteral("Response too small to be a listfile.");
        return false;
      }

      QByteArray view = data;
      if (view.startsWith("\xef\xbb\xbf"))
        view = view.mid(3);

      QString const view_text = QString::fromUtf8(view);
      if (view_text.startsWith("<!DOCTYPE", Qt::CaseInsensitive) || view_text.startsWith("<html", Qt::CaseInsensitive)
          || view_text.startsWith("<!doctype", Qt::CaseInsensitive))
      {
        if (error_message)
          *error_message = QStringLiteral("Server returned HTML, not CSV (check build slug and URL).");
        return false;
      }

      int i = 0;
      while (i < view.size() && (view[i] == '\r' || view[i] == '\n' || std::isspace (static_cast<unsigned char>(view[i]))))
        ++i;

      int j = i;
      while (j < view.size() && std::isdigit (static_cast<unsigned char>(view[j])))
        ++j;

      if (j == i || j >= view.size() || view[j] != ';')
      {
        if (error_message)
          *error_message = QStringLiteral("Response is not listfile CSV (expected `id;path` on first line).");
        return false;
      }

      return true;
    }

    QString build_url (QString const& url_template, QString const& build_slug)
    {
      QString t = url_template;
      if (t.contains(QStringLiteral("%s")))
        t.replace(QStringLiteral("%s"), build_slug, Qt::CaseSensitive);
      else if (t.contains(QStringLiteral("%1")))
        t = t.arg(build_slug);
      else
        t = t + build_slug;
      return t;
    }
  }

  bool wow_export_download_listfile_csv (QString const& url_template
                                        , QString const& build_slug
                                        , std::filesystem::path const& dest_csv
                                        , QString* error_message)
  {
    if (build_slug.isEmpty())
    {
      if (error_message)
        *error_message = QStringLiteral("Build slug is empty.");
      return false;
    }

    QString const url_str = build_url (url_template, build_slug);
    QUrl const url (url_str);
    if (!url.isValid() || url.scheme().isEmpty())
    {
      if (error_message)
        *error_message = QStringLiteral("Invalid URL: %1").arg(url_str);
      return false;
    }

    QNetworkAccessManager nam;
    QNetworkRequest req (url);
    req.setHeader (QNetworkRequest::UserAgentHeader, QStringLiteral("NoggitPurple/1.0 (listfile)"));
    QNetworkReply* reply = nam.get (req);

    QEventLoop loop;
    QObject::connect (reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    QTimer timer;
    timer.setSingleShot (true);
    timer.setInterval (120000);
    QObject::connect (&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start();
    loop.exec();

    if (!timer.isActive())
    {
      reply->abort();
      if (error_message)
        *error_message = QStringLiteral("Download timed out.");
      reply->deleteLater();
      return false;
    }
    timer.stop();

    if (reply->error() != QNetworkReply::NoError)
    {
      if (error_message)
        *error_message = reply->errorString();
      reply->deleteLater();
      return false;
    }

    QByteArray const data = reply->readAll();
    reply->deleteLater();

    if (!body_looks_like_listfile_csv (data, error_message))
      return false;

    QString const dest_q = QString::fromStdString (dest_csv.generic_string());
    QFile out (dest_q);
    if (!out.open (QIODevice::WriteOnly | QIODevice::Truncate))
    {
      if (error_message)
        *error_message = QStringLiteral("Cannot write %1").arg(dest_q);
      return false;
    }
    if (out.write (data) != data.size())
    {
      if (error_message)
        *error_message = QStringLiteral("Short write to %1").arg(dest_q);
      return false;
    }
    out.close();
    return true;
  }
}
