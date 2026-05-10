#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QSettings>
#include <QSplashScreen>
#include <QTimer>

#include "playlistwindow.h"
#include "skinutils.h"
#include "translator.h"
#include "winampbitmaps.h"
#include "winampwindow.h"
#include "qt5compat.h"

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  app.setApplicationName("Qtamp");
  app.setApplicationVersion("0.5 BETA");
  app.setOrganizationName("Qtamp");

  // Load the Winamp icon from the source resource directory
  QString appDir = QCoreApplication::applicationDirPath();
  QStringList iconCandidates = {
      appDir + "/../share/winamp/resource/WinampIcon.ico",
      appDir + "/../share/icons/hicolor/256x256/apps/winamp.png",
      "/usr/share/winamp/resource/WinampIcon.ico",
      "/usr/share/icons/hicolor/256x256/apps/winamp.png",
      appDir + "/../../Src/Winamp/resource/WinampIcon.ico",
      appDir + "/../Src/Winamp/resource/WinampIcon.ico",
      appDir + "/WinampIcon.ico"};
  for (const QString &iconPath : iconCandidates) {
    if (QFile::exists(iconPath)) {
      app.setWindowIcon(QIcon(iconPath));
      break;
    }
  }

  // Load bitmaps — try saved skin, then project skins/default, then resource
  // dir
  QSettings settings(configPath(), QSettings::IniFormat);
  QString skinPath = settings.value("skin").toString();

  // Load language pack
  QString langCode = settings.value("language", "en").toString();
  Translator::instance().loadLanguage(langCode);

  // Build a list of candidate paths
  QStringList candidates;
  if (!skinPath.isEmpty())
    candidates << skinPath;

  // FHS-standard paths for system-wide install (/usr/bin -> /usr/share/winamp)
  candidates << appDir + "/../share/winamp/skins/default"
             << appDir + "/../share/winamp/resource";

  // Absolute FHS paths (fallback if appDir is not /usr/bin)
  candidates << "/usr/share/winamp/skins/default"
             << "/usr/share/winamp/resource"
             << "/usr/local/share/winamp/skins/default"
             << "/usr/local/share/winamp/resource";

  // Paths relative to the executable (development builds)
  candidates << appDir + "/../skins/default" << appDir + "/../../skins/default"
             << QDir::homePath() + "/.winamp/skins/default";

  // Also try the source resource directory (has MAIN.BMP etc.)
  candidates << appDir + "/../Src/Winamp/resource"
             << appDir + "/../../Src/Winamp/resource";

  // Modern skins are not yet supported on Linux — always load classic skin.
  // bool savedIsModern = !skinPath.isEmpty() && isModernSkinDir(skinPath);

  bool loaded = false;
  for (const QString &path : candidates) {
    QDir d(path);
    // Skip modern skin paths when loading classic bitmaps
    if (isModernSkinDir(d.absolutePath()))
      continue;
    if (d.exists() && WinampBitmaps::instance().loadAll(d.absolutePath())) {
      qDebug() << "Successfully loaded authentic Winamp bitmaps from:"
               << d.absolutePath();
      loaded = true;
      break;
    }
  }

  // If no classic skin loaded yet and saved skin was modern, load default
  // classic as fallback
  if (!loaded) {
    QStringList fallbackClassic = {appDir + "/../share/winamp/skins/default",
                                   appDir + "/../share/winamp/resource",
                                   "/usr/share/winamp/skins/default",
                                   "/usr/share/winamp/resource",
                                   "/usr/local/share/winamp/skins/default",
                                   "/usr/local/share/winamp/resource",
                                   appDir + "/../skins/default",
                                   appDir + "/../../skins/default",
                                   QDir::homePath() + "/.winamp/skins/default",
                                   appDir + "/../Src/Winamp/resource",
                                   appDir + "/../../Src/Winamp/resource"};
    for (const QString &path : fallbackClassic) {
      QDir d(path);
      if (d.exists() && WinampBitmaps::instance().loadAll(d.absolutePath())) {
        loaded = true;
        break;
      }
    }
  }

  // Also try loading any missing bitmaps from all other candidate paths
  if (loaded) {
    for (const QString &path : candidates) {
      QDir d(path);
      if (d.exists()) {
        WinampBitmaps::instance().loadMissing(d.absolutePath());
      }
    }
  }

  if (!loaded) {
    qWarning() << "Could not load Winamp skin bitmaps from any candidate path.";
  }

  // Splash screen (matches Windows SPLASH.cpp)
  QPixmap splashPix;
  QSplashScreen *splash = nullptr;
  QStringList splashCandidates = candidates;
  splashCandidates << appDir + "/../Src/resources"
                   << appDir + "/../../Src/resources";
  for (const QString &path : splashCandidates) {
    QString splashFile = QDir(path).filePath("SPLASH.BMP");
    if (QFile::exists(splashFile)) {
      splashPix.load(splashFile);
      break;
    }
    splashFile = QDir(path).filePath("splash.bmp");
    if (QFile::exists(splashFile)) {
      splashPix.load(splashFile);
      break;
    }
  }
  if (!splashPix.isNull()) {
    splash = new QSplashScreen(splashPix);
    splash->show();
    app.processEvents();
  }

  WinampWindow w;

  // Modern skins are not yet supported on Linux — skip modern skin loading.
  // if (savedIsModern) {
  //     w.onSkinChanged(skinPath);
  // }

  // Process command-line arguments (matches Windows cmdline.cpp)
  QStringList filesToPlay;
  bool enqueueMode = false;
  for (int i = 1; i < argc; i++) {
    QString arg = QString::fromLocal8Bit(argv[i]);
    if (arg == "-enqueue" || arg == "--enqueue") {
      enqueueMode = true;
    } else if (arg == "-play" || arg == "--play") {
      // Will auto-play after adding files
    } else if (arg == "-pause" || arg == "--pause") {
      QTimer::singleShot(100, &w, [&w]() {
        if (PLAYBACK_STATE(w.getPlayer()) == QMediaPlayer::PlayingState)
          w.getPlayer()->pause();
      });
    } else if (arg == "-stop" || arg == "--stop") {
      QTimer::singleShot(100, &w, [&w]() { w.getPlayer()->stop(); });
    } else if (arg.startsWith("-")) {
      // Unknown flag — ignore
    } else {
      QFileInfo fi(arg);
      if (fi.isDir()) {
        QDir dir(arg);
        QStringList audioExts = {"*.mp3", "*.wav", "*.ogg", "*.flac",
                                 "*.m4a", "*.aac", "*.wma", "*.opus"};
        for (const QFileInfo &entry :
             dir.entryInfoList(audioExts, QDir::Files, QDir::Name))
          filesToPlay << entry.absoluteFilePath();
      } else if (fi.exists()) {
        filesToPlay << fi.absoluteFilePath();
      }
    }
  }

  // Add CLI files to playlist and optionally play
  if (!filesToPlay.isEmpty()) {
    PlaylistWindow *pl = w.getPlaylistWindow();
    if (pl) {
      for (const QString &f : filesToPlay)
        pl->addTrack(f);
      if (!enqueueMode && pl->trackCount() > 0) {
        pl->setCurrentTrackIndex(0);
        w.playTrack(pl->trackAt(0));
      }
    }
  }

  if (splash) {
    QTimer::singleShot(1500, splash, &QSplashScreen::close);
  }

  w.show();

  return app.exec();
}
