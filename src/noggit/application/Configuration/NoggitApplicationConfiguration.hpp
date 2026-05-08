// This file is part of Noggit3, licensed under GNU General Public License (version 3).
#ifndef NOGGIT_APPLICATION_CONFIGURATION_HPP
#define NOGGIT_APPLICATION_CONFIGURATION_HPP

#include <string>
#include <QSurfaceFormat>

namespace Noggit::Application {

    struct NoggitApplicationLoggingConfiguration
    {
        std::string ApplicationLoggingPath;
        bool EnableConsoleLogging;
    };

    struct NoggitApplicationGraphicsConfiguration
    {
        QSurfaceFormat::SwapBehavior SwapChainDepth = QSurfaceFormat::TripleBuffer; // DefaultSwapBehavior = 0, SingleBuffer = 1, DoubleBuffer = 2, TripleBuffer = 3
        char SwapChainInternal = 0; // 0 = no vsync, 1 = vsync, waits for one refresh cycle before swapping buffers. 2+ wait for more cycles.
        char DepthBufferSize = 24;
        char SamplesCount = 0; // No MultiSamplingAA(MSAA) = 0, Low = 2, Medium = 4, High = 8, VeryHigh = 16
    };

    struct NoggitApplicationConfiguration
    {
        std::string ApplicationProjectPath;
        std::string ApplicationThemePath;
        std::string ApplicationListFilePath;
        //! Retail (CASC): if non-empty, download listfile CSV before opening storage (default: wowdev community listfile).
        //! Use `%s` or `%1` for a build slug (e.g. wow.export); omit placeholders for a fixed URL.
        std::string ApplicationListfileWowExportUrlTemplate =
          "https://github.com/wowdev/wow-listfile/releases/latest/download/community_listfile.csv";
        std::string ApplicationListfileWowExportBuild;
        //! If true, re-download every project open; otherwise only when `listfile.csv` is missing in the project folder.
        bool ApplicationListfileWowExportAlwaysDownload = false;
        std::string ApplicationDatabaseDefinitionsPath;
        std::string ApplicationNoggitDefinitionsPath = "noggit-definitions"; // default for compatibility with older config files
        NoggitApplicationGraphicsConfiguration GraphicsConfiguration;
        NoggitApplicationLoggingConfiguration LoggingConfiguration;

        // TODO move setting panel variables here
        bool modern_features = false;
    };
}

#endif // NOGGIT_APPLICATION_CONFIGURATION_HPP