// FakeHost — the deterministic scripted PlayerHost (Wasabi 2, V2).
//
// Lets the frontend gate itself without any player: `qtamp --fakehost`
// renders against this host, and the pixel/corpus suites run on it
// byte-identically to the QtampHost idle state (calibrated: stopped,
// nothing loaded, volume 70, pan centre, EQ flat).  Writes mutate the
// canned state deterministically and fire the PlayerHost change
// signals, so interaction screenshots (CLICK_AT gates) are stable too.
// No clocks, no I/O: position only moves via seek.
//
// Moves into qtWasabi with the remote family in V3.
#pragma once

#include <QFileInfo>
#include <QUrl>

#include "playerhost.h"

class FakeHost : public PlayerHost {
    Q_OBJECT
public:
    explicit FakeHost(QObject *parent = nullptr) : PlayerHost(parent) {}

    // ── transport (idle-calibrated) ─────────────────────────────────
    qint64 positionMs() const override { return m_positionMs; }
    qint64 durationMs() const override { return m_durationMs; }
    bool isPlaying() const override { return m_playing && !m_paused; }
    bool isPaused() const override { return m_paused; }
    int volume() const override { return m_volume; }

    void play() override {
        m_playing = true;
        m_paused = false;
        emit playbackStateChanged();
    }
    void pause() override {
        if (!m_playing) return;
        m_paused = !m_paused;
        emit playbackStateChanged();
    }
    void stop() override {
        m_playing = false;
        m_paused = false;
        m_positionMs = 0;
        emit playbackStateChanged();
    }
    void next() override {}
    void prev() override {}
    void seekMs(qint64 ms) override {
        m_positionMs = qBound<qint64>(0, ms, m_durationMs);
    }
    void setVolume(int v) override { m_volume = qBound(0, v, 100); }

    // ── track ───────────────────────────────────────────────────────
    QString songTitle() const override { return m_title; }
    QString songFilename() const override {
        return m_path.isEmpty() ? QString()
                                : QFileInfo(m_path).fileName();
    }
    QString songPath() const override { return m_path; }
    QUrl currentSourceUrl() const override {
        return m_path.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_path);
    }

    // ── sliders ─────────────────────────────────────────────────────
    double sliderPosition(const QString &action,
                          const QString &param) const override {
        if (action.compare(QStringLiteral("PAN"), Qt::CaseInsensitive) == 0)
            return m_pan;
        return PlayerHost::sliderPosition(action, param);
    }
    void setSliderPosition(const QString &action, double v,
                           const QString &param) override {
        if (action.compare(QStringLiteral("PAN"), Qt::CaseInsensitive) == 0) {
            m_pan = qBound(0.0, v, 1.0);
            return;
        }
        PlayerHost::setSliderPosition(action, v, param);
    }

    // ── playlist ────────────────────────────────────────────────────
    int playlistRowCount() const override { return m_rows.size(); }
    QString playlistRowText(int row) const override {
        return m_rows.value(row).text;
    }
    qint64 playlistRowDurationMs(int row) const override {
        return m_rows.value(row).durationMs;
    }
    int playlistCurrentRow() const override { return m_currentRow; }
    void playlistSetCurrentRow(int row) override {
        if (row < 0 || row >= m_rows.size()) return;
        m_currentRow = row;
        emit playlistChanged();
    }
    void playlistPlayRow(int row) override {
        if (row < 0 || row >= m_rows.size()) return;
        m_currentRow = row;
        loadRow(row);
        play();
        emit playlistChanged();
    }

    // ── PlayerHost extras ───────────────────────────────────────────
    void openPath(const QUrl &url) override {
        addRow(url.toLocalFile().isEmpty() ? url.toString()
                                           : url.toLocalFile());
        playlistPlayRow(m_rows.size() - 1);
    }
    void enqueueAndPlay(const QUrl &url, bool enqueueOnly) override {
        addRow(url.toLocalFile().isEmpty() ? url.toString()
                                           : url.toLocalFile());
        if (!enqueueOnly) playlistPlayRow(m_rows.size() - 1);
        emit playlistChanged();
    }

    // ── ML panel sections: canned, deterministic ────────────────────
    QList<qtWasabi::Host::MlPanelItem> mlPanelChildren(
        const QString &ns) const override {
        if (ns == QLatin1String("playlists"))
            return {{QStringLiteral("Fake Mixtape"),
                     QStringLiteral("fake-mixtape.m3u")},
                    {QStringLiteral("Fake Party"),
                     QStringLiteral("fake-party.m3u8")}};
        if (ns == QLatin1String("bookmarks"))
            return {{QStringLiteral("fake://stream/one"),
                     QStringLiteral("fake://stream/one")}};
        if (ns == QLatin1String("history"))
            return {{QStringLiteral("fake-yesterday.mp3"),
                     QStringLiteral("/fake/history/fake-yesterday.mp3")}};
        if (ns == QLatin1String("devices"))
            return {{QStringLiteral("FAKESTICK"),
                     QStringLiteral("/run/media/fake/FAKESTICK")}};
        return {};
    }

private:
    struct Row {
        QString text;
        qint64 durationMs = 0;
    };
    void addRow(const QString &path) {
        m_rows.append(
            {QFileInfo(path).completeBaseName(), 175000});
        m_paths.append(path);
    }
    void loadRow(int row) {
        m_title = m_rows.value(row).text;
        m_path = m_paths.value(row);
        m_durationMs = m_rows.value(row).durationMs;
        m_positionMs = 0;
        emit sourceChanged();
        emit metaDataChanged();
    }

    bool m_playing = false;
    bool m_paused = false;
    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
    int m_volume = 70;
    double m_pan = 0.5;
    QString m_title;
    QString m_path;
    QList<Row> m_rows;
    QStringList m_paths;
    int m_currentRow = -1;
};
