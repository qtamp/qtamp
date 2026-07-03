#pragma once
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// MediaLibraryIndex — a DuckDB + Parquet backed index of a local music
// collection.  It scans a folder, reads each track's tags synchronously
// (src/audiometa), stores one row per track in a DuckDB table persisted
// as a Parquet snapshot, and answers the three aggregate queries the
// Media Library's artist -> album -> track panes need.
//
// DuckDB (not SQLite) is the mandated store for qtamp; the prebuilt C
// library is vendored under deps/duckdb by scripts/fetch-duckdb.sh.  When
// qtamp is built WITHOUT DuckDB (QTAMP_HAVE_DUCKDB undefined) every method
// is a safe no-op returning an empty result, so the Media Library simply
// shows an empty collection instead of failing to build.

#include <QList>
#include <QString>

namespace qtamp {

struct MlArtist {
    QString name;
    int     albumCount = 0;
    int     trackCount = 0;
};
struct MlAlbum {
    QString name;
    int     year       = 0;
    int     trackCount = 0;
};
struct MlTrack {
    QString artist;
    QString album;
    QString title;
    QString genre;
    int     track    = 0;
    int     year     = 0;
    qint64  lengthMs = 0;
    QString path;
};

class MediaLibraryIndex {
public:
    MediaLibraryIndex();
    ~MediaLibraryIndex();
    MediaLibraryIndex(const MediaLibraryIndex &)            = delete;
    MediaLibraryIndex &operator=(const MediaLibraryIndex &) = delete;

    // Open an in-memory DuckDB and, if a persisted Parquet snapshot
    // exists under `cacheDir`, load it.  `cacheDir` also receives the
    // snapshot on rescan().  Returns false when DuckDB is unavailable.
    bool open(const QString &cacheDir);
    bool isOpen() const;

    // Walk `root` recursively, read tags, rebuild the table, and persist
    // the Parquet snapshot.  Returns the number of tracks indexed.
    int  rescan(const QString &root);

    // Aggregate queries.  An empty `artist`/`album` means "all".  The
    // artist dimension is the album-artist when present, else the track
    // artist (matching real Winamp's ml_local grouping).
    QList<MlArtist> artists() const;
    QList<MlAlbum>  albums(const QString &artist) const;
    QList<MlTrack>  tracks(const QString &artist, const QString &album) const;
    int             totalTracks() const;

private:
    struct Impl;
    Impl *d = nullptr;
};

}  // namespace qtamp
