// BackendServer — the loopback control channel of `qtamp --backend`.
// Exposes the running player (a local QtampHost + its playlist model,
// audio and all) over the HTTP+SSE protocol in pylon/PROTOCOL.md so a
// pylon can fan it out as GraphQL and RemoteHost heads can sync to it.
//
// Works against the PlayerHost surface plus a few hooks for the pieces
// that live outside it (playlist row removal, the EQ toggles) — the
// concrete QtampHost/PlaylistWindow types stay private to main.cpp.
// Single-threaded on the GUI event loop; binds 127.0.0.1 only.
#pragma once

#include <QElapsedTimer>
#include <QObject>

#include <functional>

#include "remotestate.h"

class QTcpServer;
class QTcpSocket;
class PlayerHost;

namespace qtamp {

class BackendServer : public QObject {
    Q_OBJECT
public:
    struct Hooks {
        std::function<void()> playlistClear;
        std::function<void(const QList<int> &)> playlistRemoveRows;
        std::function<bool()> eqOn;
        std::function<void(bool)> setEqOn;
        std::function<bool()> eqAuto;
        std::function<void(bool)> setEqAuto;
        // Absolute directory `open`/`playlistAddPaths` are confined to.
        QString musicRoot;
    };

    BackendServer(PlayerHost *host, Hooks hooks, QObject *parent = nullptr);
    bool listen(quint16 port);  // 0 = ephemeral; see port() after
    quint16 port() const;

    // Immediately re-check state and push events (wired to the host's
    // change signals and called after every applied command, so pushes
    // are prompt; the 250 ms timer only catches signal-less drift).
    void pushChanges();

private:
    void onNewConnection();
    void handleRequest(QTcpSocket *sock, const QByteArray &method,
                       const QByteArray &path, const QByteArray &ifNoneMatch,
                       const QByteArray &body);
    QJsonObject handleCmd(const QJsonObject &cmd, bool *ok);
    RemoteSnapshot buildSnapshot() const;
    void broadcast(const QByteArray &event, const QJsonObject &payload);
    QJsonObject sectionEnvelope(const char *section,
                                const QJsonObject &body) const;
    bool pathAllowed(const QString &path) const;

    static void respond(QTcpSocket *sock, int status, const QByteArray &type,
                        const QByteArray &body,
                        const QByteArray &etag = QByteArray());
    static void respondJson(QTcpSocket *sock, int status,
                            const QJsonObject &o);

    PlayerHost *m_host;
    Hooks m_hooks;
    QTcpServer *m_server = nullptr;
    QList<QTcpSocket *> m_eventSinks;
    QString m_epoch;
    quint64 m_revision = 0;
    quint64 m_playlistRevision = 0;
    bool m_playlistDirty = false;
    QElapsedTimer m_clock;  // serverNowMs / positionAtMs source
    // Last pushed per-section fingerprints (position excluded — clients
    // interpolate; only transport EDGES push).
    QByteArray m_fpTransport, m_fpTrack, m_fpPlaylist, m_fpEq;
};

}  // namespace qtamp
