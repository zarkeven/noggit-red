#include "LoadProjectComponent.hpp"
#include "ui_NoggitProjectSelectionWindow.h"

#include <noggit/application/NoggitApplication.hpp>
#include <noggit/Log.h>
#include <noggit/project/ApplicationProject.h>
#include <noggit/ui/windows/projectSelection/NoggitProjectSelectionWindow.hpp>

#include <QListWidget>
#include <QListWidgetItem>


namespace Noggit::Ui::Component
{
  std::shared_ptr<Project::NoggitProject> LoadProjectComponent::loadProject(
    Windows::NoggitProjectSelectionWindow* parent
  , QString force_project_path
  )
  {
    QString project_path;

    if (!force_project_path.isEmpty())
    {
      project_path = force_project_path;
    }
    else
    {
      QListWidgetItem* const item = parent->_ui->listView->currentItem();
      if (!item)
      {
        LogError << "No project selected in recent projects list." << std::endl;
        return {};
      }

      project_path = item->data (Qt::UserRole).toString();
    }

    if (project_path.isEmpty())
    {
      LogError << "Selected project has an empty path." << std::endl;
      return {};
    }

    auto application_configuration = parent->_noggit_application->getConfiguration();
    auto application_project_service = Noggit::Project::ApplicationProject (application_configuration);

    if (!QDir (project_path).exists())
    {
      LogError << "Project path does not exist : " << project_path.toStdString() << std::endl;
      return {};
    }

    Log << "Loading Project path : " << project_path.toStdString() << std::endl;

#if !defined(Q_OS_WIN)
    // Probe whether the filesystem is case-sensitive. Use the project folder on Unix only;
    // on Windows NTFS is case-insensitive and creating both probe names collides.
    QDir q_project_path { project_path };

    bool is_case_sensitive_fs = false;
    QString const file_1_path { q_project_path.filePath ("__noggit_fs_test.t") };
    QString const file_2_path { q_project_path.filePath ("__NOGGIT_FS_TEST.t") };

    QFile file_1 { file_1_path };
    if (!file_1.open (QIODevice::ReadWrite))
    {
      LogError << "Failed to open file : " << file_1_path.toStdString() << std::endl;
      return {};
    }
    {
      QTextStream stream (&file_1);
      stream << "a" << Qt::endl;
    }
    file_1.close();

    QFile file_2 { file_2_path };
    if (!file_2.open (QIODevice::ReadWrite))
    {
      LogError << "Failed to open file : " << file_2_path.toStdString() << std::endl;
      file_1.remove();
      return {};
    }
    {
      QTextStream stream (&file_2);
      stream << "b" << Qt::endl;
    }
    file_2.close();

    QFile file_test { file_1_path };
    if (file_test.open (QIODevice::ReadOnly))
    {
      QTextStream stream (&file_test);
      QString const line = stream.readLine();
      if (line.contains ("a"))
      {
        is_case_sensitive_fs = true;
      }
    }
    else
    {
      LogError << "Failed to read file content : " << file_1_path.toStdString() << std::endl;
      file_1.remove();
      file_2.remove();
      return {};
    }
    file_test.close();
    file_1.remove();

    if (is_case_sensitive_fs)
    {
      file_2.remove();

      bool has_uppercase = false;
      QDirIterator it (project_path, QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);

      while (it.hasNext())
      {
        QString const filepath = it.next();
        QString const remainder = q_project_path.relativeFilePath (filepath);

        if (!remainder.isLower())
        {
          has_uppercase = true;
          break;
        }
      }

      if (has_uppercase)
      {
        QMessageBox prompt;
        prompt.setWindowIcon (QIcon (":/icon"));
        prompt.setWindowTitle ("Convert project?");
        prompt.setIcon (QMessageBox::Warning);
        prompt.setWindowFlags (Qt::WindowStaysOnTopHint);
        prompt.setText ("Your project contains upper-case named files, "
                        "which won't be visible to Noggit running on your OS with case-sensitive filesystems. "
                        "Do you want to fix your filenames?");
        prompt.addButton ("Accept", QMessageBox::AcceptRole);
        prompt.setDefaultButton (prompt.addButton ("Cancel", QMessageBox::RejectRole));
        prompt.setWindowFlags (Qt::CustomizeWindowHint | Qt::WindowTitleHint);

        prompt.exec();

        switch (prompt.buttonRole (prompt.clickedButton()))
        {
          case QMessageBox::AcceptRole:
          {
            std::vector<QString> incorrect_paths;

            QDirIterator scan (project_path, QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);

            while (scan.hasNext())
            {
              QString const filepath = scan.next();
              if (!filepath.isLower())
              {
                incorrect_paths.push_back (filepath);
              }
            }

            std::sort (incorrect_paths.begin(), incorrect_paths.end(), std::greater<QString>{});

            for (auto& path : incorrect_paths)
            {
              QFileInfo f_info_path { path };
              QString const filename_lower = f_info_path.fileName().toLower();
              QDir path_dir = f_info_path.dir();
              QFile::rename (path, path_dir.filePath (filename_lower));
            }

            break;
          }
          case QMessageBox::DestructiveRole:
          default:
            LogError << "User declined uppercase filename conversion." << std::endl;
            return {};
        }
      }
    }
#endif

    auto project = application_project_service.loadProject (project_path.toStdString());

    if (project)
    {
      Noggit::Application::NoggitApplication::instance()->setClientData (project->ClientData);
    }
    else
    {
      LogError << "Couldn't set client data : Project loading failed." << std::endl;
    }

    return project;
  }
}
