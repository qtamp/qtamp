// PlayerHost — the qtamp integration base over qtWasabi::Host.
//
// The engine (deps/qtWasabi) reaches the player exclusively through the
// abstract qtWasabi::Host vtable.  The qtamp integration layer
// (QtampPlayerWindow, main()) needs a slightly larger surface than that
// vtable: a handful of concrete methods (open a path, enqueue, the EQ
// band store, the vis analyzer, the current source URL) plus change
// notifications that today come straight off QMediaPlayer's signals.
//
// PlayerHost gathers exactly that surface so more than one Host
// implementation can back the same window: QtampHost (local audio) and
// RemoteHost (a networked player synced over GraphQL).  The window and
// main() depend only on PlayerHost, never on a concrete subclass — the
// factory in main() decides which one to build.
#pragma once

#include <QObject>
#include <QUrl>
#include <QList>

#include <functional>

#include <qtWasabi/Host.h>

class QtampPlayerWindow;
class AudioAnalyzer;

class PlayerHost : public QObject, public qtWasabi::Host {
    Q_OBJECT
public:
    using QObject::QObject;

    // ── Playback entry points the window/menus call directly ──────────
    // Decode and play a path from the top (pledit double-click, CLI).
    virtual void openPath(const QUrl &u) = 0;
    // Enqueue (optionally without starting) and play.
    virtual void enqueueAndPlay(const QUrl &u, bool enqueueOnly = false) = 0;
    // File/folder pickers — local-only; remote hosts return empty.
    virtual QList<QUrl> openFilesAndEnqueue(QWidget *embedder,
                                            bool enqueueOnly = false) {
        Q_UNUSED(embedder);
        Q_UNUSED(enqueueOnly);
        return {};
    }
    virtual QList<QUrl> openFolderAndEnqueue(QWidget *embedder,
                                             bool enqueueOnly = false) {
        Q_UNUSED(embedder);
        Q_UNUSED(enqueueOnly);
        return {};
    }

    // ── State the window pulls that isn't on the Host vtable ──────────
    // Full filesystem path of the current item (Maki "playitem:string").
    virtual QString songPath() const { return songFilename(); }
    // The current media source (bookmarks "add current", About skin path).
    virtual QUrl currentSourceUrl() const = 0;
    // Reload spectrum/peak visual preferences.
    virtual void reloadVisPrefs() {}
    // The vis analyzer for the MilkDrop overlay; null = no local analyzer
    // (a remote host has no PCM to feed it, and the overlay is skipped).
    virtual AudioAnalyzer *analyzerPtr() { return nullptr; }
    // Bind the owning window for window-control callbacks.
    virtual void bindWindow(QtampPlayerWindow *w) { m_window = w; }

    // ── EQ band store (Maki System.setEqBand/getEqBand, -127..127) ────
    // Default routes through the EQ_BAND slider axis already on the Host
    // vtable, so a RemoteHost gets working EQ callbacks for free;
    // QtampHost overrides these with its direct DSP store.
    virtual void setEqBandValue(int band, int val) {
        // Winamp band scale is -127..127; the slider axis is 0..1.
        setSliderPosition(QStringLiteral("EQ_BAND"),
                          qBound(0.0, (val + 127) / 254.0, 1.0),
                          QString::number(band));
    }
    virtual int eqBandValue(int band) const {
        const double p = sliderPosition(QStringLiteral("EQ_BAND"),
                                        QString::number(band));
        if (p < 0.0) return 0;
        return qRound(p * 254.0) - 127;
    }

    // Local hosts own files, a library and native dialogs; remote hosts
    // do not — the window disables those affordances when true.
    virtual bool isRemote() const { return false; }

    // Opens the Preferences dialog (wired by the window that owns it).
    std::function<void()> showPreferencesFn;

    // External model owners (the PlaylistWindow) report row changes here;
    // signals cannot be emitted from outside the class.
    void notifyPlaylistChanged() { emit playlistChanged(); }

signals:
    // Replace the direct QMediaPlayer signal connections in the window,
    // so a remote host drives the same repaint/title machinery.
    void sourceChanged();
    void playbackStateChanged();
    void metaDataChanged();
    void playlistChanged();

protected:
    QtampPlayerWindow *m_window = nullptr;
};
