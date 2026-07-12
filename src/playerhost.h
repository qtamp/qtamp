// PlayerHost — qtamp's shim over qtWasabi::PlayerHost.
//
// The framework base (public/qtWasabi/PlayerHost.h) carries everything
// a head needs from a host.  qtamp adds the two local-only hooks its
// window layer still reaches for directly: the vis analyzer for the
// MilkDrop overlay, and the QtampPlayerWindow binding the window-
// control Host methods (close/minimize/shade/sysmenu) act through.
// Framework hosts (remote::RemoteHost, FakeHost) don't carry this shim;
// call sites that need it dynamic_cast and skip when it isn't there.
#pragma once

#include <qtWasabi/PlayerHost.h>

class QtampPlayerWindow;
class AudioAnalyzer;

class PlayerHost : public qtWasabi::PlayerHost {
    Q_OBJECT
public:
    using qtWasabi::PlayerHost::PlayerHost;

    // The vis analyzer for the MilkDrop overlay; null = no local
    // analyzer (nothing feeds PCM, the overlay is skipped).
    virtual AudioAnalyzer *analyzerPtr() { return nullptr; }

    // Bind the owning window for window-control callbacks.
    virtual void bindWindow(QtampPlayerWindow *w) { m_window = w; }

protected:
    QtampPlayerWindow *m_window = nullptr;
};
