#ifndef QT5COMPAT_H
#define QT5COMPAT_H

#include <QtGlobal>
#include <QMouseEvent>

// Qt5/Qt6 compatibility macros for mouse event APIs
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  #define GLOBAL_POS(event)  (event)->globalPosition().toPoint()
  #define EVENT_POS(event)   (event)->position().toPoint()
#else
  #define GLOBAL_POS(event)  (event)->globalPos()
  #define EVENT_POS(event)   (event)->pos()
#endif

// Qt5/Qt6 compatibility for QMediaPlayer state
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  #define PLAYBACK_STATE(p)  (p)->playbackState()
#else
  #define PLAYBACK_STATE(p)  (p)->state()
#endif

#endif // QT5COMPAT_H
