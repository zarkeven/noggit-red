// This file is part of Noggit3, licensed under GNU General Public License (version 3).
#include <noggit/application/Configuration/NoggitApplicationConfiguration.hpp>
#include <noggit/application/NoggitApplication.hpp>
#include <noggit/errorHandling.h>
#include <noggit/ui/windows/projectSelection/NoggitProjectSelectionWindow.hpp>

#include <qcommandlineoption.h>
#include <qcommandlineparser.h>
#include <QStyleFactory>
#include <QtCore/QSettings>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFileDialog>

QCommandLineParser* ProcessCommandLine()
{
    QCommandLineParser* parser = new QCommandLineParser();
    parser->setApplicationDescription("Help");
    parser->addHelpOption();
    parser->addVersionOption();
    parser->addOptions({
        {"disable-update", QApplication::translate("main", "Disable the check for update.")},
        {"force-changelog", QApplication::translate("main", "Force displaying the changelog popup.")}
        });

    return parser;
}

int main(int argc, char *argv[])
{
  Noggit::RegisterErrorHandlers();
  std::set_terminate(Noggit::Application::NoggitApplication::terminationHandler);

  // Tablet backend selection must happen before QApplication is created.
#ifdef _WIN32
  {
    QCoreApplication::setOrganizationName("Noggit");
    QCoreApplication::setApplicationName("Noggit");
    QSettings settings;
    bool const use_windows_ink = settings.value("tablet/use_windows_ink", true).toBool();
    if (use_windows_ink)
      qputenv("QT_USE_WININK", "1");
    else
      qunsetenv("QT_USE_WININK");
  }
#endif

  QApplication::setStyle(QStyleFactory::create("Fusion"));
  QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
  QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  QCoreApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTabletEvents, false);
  QApplication q_application (argc, argv);
  q_application.setApplicationName ("Noggit");
  q_application.setOrganizationName ("Noggit");

  auto parser = ProcessCommandLine();
  parser->process(q_application);

  std::vector<bool> Command;
  Command.push_back(parser->isSet("disable-update"));
  Command.push_back(parser->isSet("force-changelog"));

  auto noggit = Noggit::Application::NoggitApplication::instance();
  bool initialized = noggit->initalize(argc, argv, Command);

  if (!initialized) [[unlikely]]
  {
    return EXIT_FAILURE;
  }

  auto project_selection = new Noggit::Ui::Windows::NoggitProjectSelectionWindow(noggit);
  // project_selection->show();

  return q_application.exec();
}
