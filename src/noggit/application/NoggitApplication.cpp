#include <noggit/application/NoggitApplication.hpp>
#include <noggit/AsyncLoader.h>
#include <noggit/application/Configuration/NoggitApplicationConfigurationWriter.hpp>
#include <noggit/application/Configuration/NoggitApplicationConfigurationReader.hpp>
#include <noggit/application/Configuration/NoggitApplicationConfiguration.hpp>
#include <opengl/context.hpp>
#include <opengl/context.inl>
#include <noggit/Log.h>
#include <revision.h>
#include <util/exception_to_string.hpp>
#include <ClientData.hpp>

#include <chrono>
#include <thread>
#include <filesystem>

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QLocale>
#include <QMessageBox>
#include <QString>
#include <QSettings>
#include <QOffscreenSurface>
#include <QOpenGLContext>

namespace
{
  std::atomic_bool success = false;
  
  void opengl_context_creation_stuck_failsafe()
  {
  	for (int i = 0; i < 50; ++i)
  	{
  		std::this_thread::sleep_for(std::chrono::milliseconds(100));
  		if (success.load())
  		{
  			return;
  		}
  	}
  
  	LogError << "OpenGL Context creation failed (timeout), closing..." << std::endl;
  
  	std::terminate();
  }
}

namespace Noggit::Application
{
	NoggitApplication* NoggitApplication::instance()
	{
		static NoggitApplication inst{};
		return &inst;
	}

	BlizzardArchive::ClientData* NoggitApplication::clientData()
  {
    return _client_data.get();
  }

  bool NoggitApplication::hasClientData() const
  {
    return _client_data != nullptr;
  }

  void NoggitApplication::setClientData(std::shared_ptr<BlizzardArchive::ClientData> data)
  {
    _client_data = data;
  }

  bool NoggitApplication::initalize(int argc, char* argv[], std::vector<bool> Parser)
  {
	  Command = Parser;

	  // Log next to the executable when argv[0] carries a directory; otherwise cwd.
	  std::filesystem::path log_dir = std::filesystem::current_path();
	  if (argv && argv[0])
	  {
	    std::filesystem::path const exe_path (argv[0]);
	    if (exe_path.has_parent_path())
	    {
	      std::filesystem::path const parent = exe_path.parent_path();
	      if (!parent.empty())
	      {
	        log_dir = parent;
	      }
	    }
	  }
	  InitLogging (log_dir);

	  Log << "Load trace (AsyncLoader / ADT / previews): "
	      << (LoadTraceEnabled() ? "on" : "off")
	      << " (env NOGGIT_LOAD_TRACE, QSettings load_trace, or additional_file_loading_log)"
	      << std::endl;

	  QLocale locale = QLocale(QLocale::English);
	  QString dateTimeText = locale.toString(QDateTime::currentDateTime(), "dd MMMM yyyy hh:mm:ss");
	  Log << "Start time : " << dateTimeText.toStdString() << std::endl;

	 //Locate application relative path
	  Log << "Noggit Studio - " << STRPRODUCTVER << std::endl;
	  Log << "Build Date : " << __DATE__ ", " __TIME__ << std::endl;

	  auto applicationLocation = std::filesystem::path(argv[0]);
	  Log << "Noggit Application Path: " << applicationLocation << std::endl;

	  auto applicationExecutionLocation = std::filesystem::current_path();
	  Log << "Noggit Execution Path: " << applicationExecutionLocation << std::endl;

	  if (applicationLocation.remove_filename().is_relative())
	  {
		  std::filesystem::current_path(std::filesystem::current_path() / applicationLocation);
	  }
	  else
	  {
		  std::filesystem::current_path(applicationLocation);
	  }

	  auto applicationCurrentPath = std::filesystem::current_path();
	  Log << "Noggit Relative Path: " << applicationCurrentPath << std::endl;

	  //Locate application configuration file
	  auto nogginConfigurationPath = applicationCurrentPath / "noggit.json";
	
	  if(!std::filesystem::exists(nogginConfigurationPath))
	  {
		  //Create Default config file
		  Log << "Noggit Configuration File Not Found! Creating New File: " << nogginConfigurationPath << std::endl;

		  auto configurationFileStream = QFile(QString::fromStdString(nogginConfigurationPath.generic_string()));
		  auto configurationFileWriter = NoggitApplicationConfigurationWriter();
		  configurationFileWriter.PersistDefaultConfigurationState(configurationFileStream);
		  configurationFileStream.close();
	  }

	  //Read config file
	  auto configurationFileStream = QFile(QString::fromStdString( nogginConfigurationPath.generic_string()));
	  auto configurationFileReader = NoggitApplicationConfigurationReader();
	  auto applicationConfiguration = configurationFileReader.ReadConfigurationState(configurationFileStream);

	  configurationFileStream.close();

	  Log << "Noggit Configuration File Loaded! Creating New File: " << nogginConfigurationPath << std::endl;

	  auto noggitProjectPath = applicationConfiguration.ApplicationProjectPath;
	  if (!std::filesystem::exists(noggitProjectPath))
	  {
		  // std::filesystem::create_directory(noggitProjectPath);
		  // Log << "Noggit Project Folder Not Loaded! Creating..." << std::endl;
	  }

	  auto& listFilePath = applicationConfiguration.ApplicationListFilePath;
	  if (!std::filesystem::exists(listFilePath))
	  {
		  // LogError << "Unable to find listfile! please reinstall Noggit Red, or download from wow.tools" << std::endl;
	  }

	  Log << "Listfile found! : " << listFilePath << std::endl;

	  auto& databaseDefinitionPath = applicationConfiguration.ApplicationDatabaseDefinitionsPath;
	  if (!std::filesystem::exists(databaseDefinitionPath))
	  {
		  LogError << "Unable to find database definitions! please reinstall Noggit Red, or download from wow.tools" << std::endl;
	  }
		else
		{
			Log << "Database Definitions found! : " << databaseDefinitionPath << std::endl;
		}

		auto& noggitDefinitionPath = applicationConfiguration.ApplicationNoggitDefinitionsPath;
		if (!std::filesystem::exists(noggitDefinitionPath))
		{
			LogError << "Unable to find noggit definitions! " << noggitDefinitionPath << std::endl;
		}

	  // Check MSVC redistribuable version
	  const QString registryPath = "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\VisualStudio\\";

	  // Visual C++ 2015 (MSVC++ 14.0)
	  // Visual C++ 2017 (MSVC++ 14.1)
	  // Visual C++ 2019 (MSVC++ 14.2)
	  // Visual C++ 2022 (MSVC++ 14.3)
	  const QStringList versions = {
		  "14.0", //  MSVC 14.0 Visual Studio 2015
		  // "15.0", //  MSVC 14.1 Visual Studio 2017
		  // "16.0", //  MSVC 14.2 Visual Studio 2019
		  // "17.0"  //  MSVC 14.3 Visual Studio 2022
	  };

	  // confirmed crashes with v14.30.30704.00 and v14.36.32532.00
	  const int required_version = 40;

	  bool redist_found = false;
	  foreach (const QString & version, versions) {
		  QString keyPath = registryPath + version + "\\VC\\Runtimes\\x64";
		  QSettings settings(keyPath, QSettings::NativeFormat);

		  if (settings.contains("Installed")) {
			  bool installed = settings.value("Installed").toBool();
			  if (installed) {

				  QString versionNumber = settings.value("Version").toString();
				  // LogDebug << "Minor version : " << minorVersion << std::endl;
				  LogDebug << "Found MSVC " << version.toStdString() << " Redistributable Version: " << versionNumber.toStdString() << std::endl;

				  int minorVersion = settings.value("Minor").toInt();
 				  if (minorVersion < required_version)
 				  {
 					  {
 						QMessageBox msgBox;
 						msgBox.setIcon(QMessageBox::Critical);
 						msgBox.setWindowTitle("Outdated Redistributable");
 						msgBox.setTextFormat(Qt::RichText);   //this is what makes the links clickable
 
 						QString message = "Your Microsoft Visual C++ Redistributable x64 is outdated. "
 							  "Please update it to the latest version to continue running this application.<br>"
 							  "You can download it from the <a href=\"https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170\">official website (x64)</a>.<br>"
 							"Direct Download Link : <a href=\"https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170\">https://aka.ms/vs/17/release/vc_redist.x64.exe</a>";
 
 						msgBox.setText(message);
 						msgBox.setStandardButtons(QMessageBox::Ok);
 						msgBox.exec();
 					  }
 
 					  // throw std::runtime_error("Installed Microsoft Visual C++ Redistributable version : " + versionNumber.toStdString() +  " is too old."
 					  // + "Minimum required is 14.31"
 					  // + "Update at https://aka.ms/vs/17/release/vc_redist.x64.exe or search \"Microsoft Visual C++ Redistributable x64\"");
 				  }
				  redist_found = true;
			  }
		  }
	  }
	  if (!redist_found)
	  {
		  LogDebug << "No Redistribuable MSVC version 14.xx found" << std::endl;
	  }

	  // Initialize OpenGL
	  QSurfaceFormat format;
	  
	  format.setRenderableType(QSurfaceFormat::OpenGL);
	  format.setVersion(4, 1); // will automatically set to the highest version available
	  format.setProfile(QSurfaceFormat::CoreProfile);
	  format.setSwapBehavior(applicationConfiguration.GraphicsConfiguration.SwapChainDepth); // default is TripleBuffer
	  QSettings app_settings;
	  bool vsync = app_settings.value("vsync", false).toBool();
	  format.setSwapInterval(vsync ? 1 : applicationConfiguration.GraphicsConfiguration.SwapChainInternal);
		if (applicationConfiguration.GraphicsConfiguration.SwapChainInternal > 1)
			LogDebug << "WARNING : SwapChainInternal setting is set to more than 1, this will significantly slow down rendering." << std::endl;
	  // TODO. old config files used 16 so just ignore them, could implement a version check of the config file to update it
	  format.setDepthBufferSize(24); // applicationConfiguration.GraphicsConfiguration.DepthBufferSize
	  bool doAntiAliasing = app_settings.value("anti_aliasing", false).toBool();
		// Multisample anti-aliasing (MSAA). 0x, 2x, 4x, 8x or 16x. Default is 0, no AA
	  format.setSamples(doAntiAliasing ? 4 : applicationConfiguration.GraphicsConfiguration.SamplesCount); 

	  // context creation seems to get stuck sometimes, this ensure the app is killed
	  // otherwise it's wasting cpu resources and is annoying when developping
	  // auto failsafe = std::async(&opengl_context_creation_stuck_failsafe);

	  QSurfaceFormat::setDefaultFormat(format);
	  // IMPORTANT:
	  // Do NOT create/makeCurrent any OpenGL context here.
	  // On some NVIDIA drivers, creating an early offscreen context can poison later
	  // QOpenGLWidget contexts (manifesting as bogus GL_OUT_OF_MEMORY then nvoglv64 crashes).
	  // We validate/log the real OpenGL context later when MapView initializes its QOpenGLWidget context.
	  success = true;

    _application_configuration = std::make_shared<Noggit::Application::NoggitApplicationConfiguration>(applicationConfiguration);
	  //All of the below should be Project Initalisation
	  srand(::time(nullptr));

	  // TODO : thread count setting
	  // AsyncLoader::setup(NoggitSettings.value("async_thread_count", 3).toInt());
	  AsyncLoader::setup(3);

		return true;
  }

  std::shared_ptr<Noggit::Application::NoggitApplicationConfiguration> NoggitApplication::getConfiguration()
  {
	  return _application_configuration;
  }

  void NoggitApplication::terminationHandler()
  {
	  std::string const reason{ ::util::exception_to_string(std::current_exception()) };

	  if (qApp)
	  {
		  QMessageBox::critical(nullptr
			  , "std::terminate"
			  , QString::fromStdString(reason)
			  , QMessageBox::Close
			  , QMessageBox::Close
		  );
	  }
	  LogError << "std::terminate: " << reason << std::endl;
  }

  bool NoggitApplication::GetCommand(int index)
  {
	  if (index >= 0 && index < Command.size())
		  return Command[index];

	  return false;
  }
}
