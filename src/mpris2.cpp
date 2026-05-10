#ifdef QT_DBUS_LIB

#include "mpris2.h"

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QMediaContent>
#endif

// Forward declarations for MPRIS2 methods that reference WinampWindow/PlaylistWindow
// These are implemented in mpris2_impl.cpp where the full type definitions are available
// The simple methods are here.

Mpris2RootAdaptor::Mpris2RootAdaptor(QObject *parent) : QDBusAbstractAdaptor(parent) {}

void Mpris2RootAdaptor::Raise() {
    QWidget *w = qobject_cast<QWidget*>(parent());
    if (w) { w->show(); w->raise(); w->activateWindow(); }
}

void Mpris2RootAdaptor::Quit() { QApplication::quit(); }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
Mpris2PlayerAdaptor::Mpris2PlayerAdaptor(QMediaPlayer *player, QAudioOutput *audioOut, QObject *parent)
    : QDBusAbstractAdaptor(parent), m_player(player), m_audioOutput(audioOut) {
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this]() {
        emitPropertyChanged("PlaybackStatus", playbackStatus());
    });
    connect(m_player, &QMediaPlayer::metaDataChanged, this, [this]() {
        emitPropertyChanged("Metadata", metadata());
    });
}
#else
Mpris2PlayerAdaptor::Mpris2PlayerAdaptor(QMediaPlayer *player, QObject *parent)
    : QDBusAbstractAdaptor(parent), m_player(player) {
    connect(m_player, &QMediaPlayer::stateChanged, this, [this]() {
        emitPropertyChanged("PlaybackStatus", playbackStatus());
    });
    // Qt5 overloads metaDataChanged — use metaDataAvailableChanged instead
    connect(m_player, &QMediaPlayer::metaDataAvailableChanged, this, [this](bool available) {
        Q_UNUSED(available);
        emitPropertyChanged("Metadata", metadata());
    });
}
#endif

QString Mpris2PlayerAdaptor::playbackStatus() const {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    switch (m_player->playbackState()) {
#else
    switch (m_player->state()) {
#endif
        case QMediaPlayer::PlayingState: return "Playing";
        case QMediaPlayer::PausedState: return "Paused";
        default: return "Stopped";
    }
}

QVariantMap Mpris2PlayerAdaptor::metadata() const {
    QVariantMap map;
    map["mpris:trackid"] = QVariant::fromValue(QDBusObjectPath("/org/mpris/MediaPlayer2/CurrentTrack"));
    if (m_player->duration() > 0)
        map["mpris:length"] = m_player->duration() * 1000; // microseconds
    
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QMediaMetaData meta = m_player->metaData();
    QString title = meta.stringValue(QMediaMetaData::Title);
    QString artist = meta.stringValue(QMediaMetaData::AlbumArtist);
    if (artist.isEmpty()) artist = meta.stringValue(QMediaMetaData::ContributingArtist);
    QString album = meta.stringValue(QMediaMetaData::AlbumTitle);
    QUrl source = m_player->source();
#else
    QString title = m_player->metaData("Title").toString();
    QString artist = m_player->metaData("AlbumArtist").toString();
    if (artist.isEmpty()) artist = m_player->metaData("ContributingArtist").toString();
    QString album = m_player->metaData("AlbumTitle").toString();
    QUrl source = m_player->media().canonicalUrl();
#endif

    if (!title.isEmpty()) map["xesam:title"] = title;
    if (!artist.isEmpty()) map["xesam:artist"] = QStringList{artist};
    if (!album.isEmpty()) map["xesam:album"] = album;
    if (source.isValid()) map["xesam:url"] = source.toString();
    
    return map;
}

double Mpris2PlayerAdaptor::volume() const {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return m_audioOutput->volume();
#else
    return m_player->volume() / 100.0;
#endif
}

void Mpris2PlayerAdaptor::setVolume(double v) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_audioOutput->setVolume(qBound(0.0, v, 1.0));
#else
    m_player->setVolume(qBound(0, static_cast<int>(v * 100), 100));
#endif
}

qlonglong Mpris2PlayerAdaptor::position() const { return m_player->position() * 1000; }

bool Mpris2PlayerAdaptor::canSeek() const { return m_player->duration() > 0; }

void Mpris2PlayerAdaptor::Pause() { m_player->pause(); }
void Mpris2PlayerAdaptor::PlayPause() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
#else
    if (m_player->state() == QMediaPlayer::PlayingState)
#endif
        m_player->pause();
    else
        m_player->play();
}
void Mpris2PlayerAdaptor::Stop() { m_player->stop(); }
void Mpris2PlayerAdaptor::Play() { m_player->play(); }
void Mpris2PlayerAdaptor::Seek(qlonglong offset) {
    qint64 newPos = m_player->position() + offset / 1000;
    m_player->setPosition(qBound(0LL, newPos, m_player->duration()));
}
void Mpris2PlayerAdaptor::SetPosition(const QDBusObjectPath &, qlonglong pos) {
    m_player->setPosition(pos / 1000);
}

void Mpris2PlayerAdaptor::emitPropertyChanged(const QString &property, const QVariant &value) {
    QDBusMessage msg = QDBusMessage::createSignal(
        "/org/mpris/MediaPlayer2",
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged");
    msg << "org.mpris.MediaPlayer2.Player";
    QVariantMap changedProps;
    changedProps[property] = value;
    msg << changedProps;
    msg << QStringList();
    QDBusConnection::sessionBus().send(msg);
}

// Next(), Previous(), OpenUri() need WinampWindow — implemented in winampwindow.cpp

#endif // QT_DBUS_LIB
