// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "medialibraryindex.h"

#include "audiometa.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStringList>

#ifdef QTAMP_HAVE_DUCKDB
#include "duckdb.h"
#endif

namespace qtamp {

namespace {
const QStringList &audioExts() {
    static const QStringList e = {
        QStringLiteral("mp3"),  QStringLiteral("flac"), QStringLiteral("ogg"),
        QStringLiteral("opus"), QStringLiteral("wav"),  QStringLiteral("m4a"),
        QStringLiteral("aac"),  QStringLiteral("wma"),  QStringLiteral("aiff"),
        QStringLiteral("alac") };
    return e;
}
}  // namespace

#ifdef QTAMP_HAVE_DUCKDB

struct MediaLibraryIndex::Impl {
    duckdb_database   db  = nullptr;
    duckdb_connection con = nullptr;
    QString           parquetPath;
    bool              open = false;

    ~Impl() {
        if (con) duckdb_disconnect(&con);
        if (db)  duckdb_close(&db);
    }

    // Run a statement with no result set; return true on success.
    bool exec(const QString &sql) const {
        return duckdb_query(con, sql.toUtf8().constData(), nullptr)
               == DuckDBSuccess;
    }

    static QString colStr(duckdb_result *r, idx_t col, idx_t row) {
        char *v = duckdb_value_varchar(r, col, row);
        QString s = v ? QString::fromUtf8(v) : QString();
        if (v) duckdb_free(v);
        return s;
    }

    static QString sqlQuote(const QString &s) {
        QString q = s;
        q.replace(QLatin1Char('\''), QLatin1String("''"));
        return q;
    }
};

MediaLibraryIndex::MediaLibraryIndex() : d(new Impl) {}
MediaLibraryIndex::~MediaLibraryIndex() { delete d; }

bool MediaLibraryIndex::isOpen() const { return d && d->open; }

bool MediaLibraryIndex::open(const QString &cacheDir) {
    if (duckdb_open(nullptr, &d->db) != DuckDBSuccess) return false;
    if (duckdb_connect(d->db, &d->con) != DuckDBSuccess) return false;
    QDir().mkpath(cacheDir);
    d->parquetPath = QDir(cacheDir).filePath(QStringLiteral("library.parquet"));

    static const char *kSchema =
        "CREATE TABLE tracks("
        "path VARCHAR, artist VARCHAR, albumartist VARCHAR, album VARCHAR, "
        "title VARCHAR, tracknum INTEGER, disc INTEGER, year INTEGER, "
        "genre VARCHAR, length_ms BIGINT)";

    if (QFileInfo::exists(d->parquetPath)) {
        // Load the persisted snapshot into a fresh table.
        const QString load = QStringLiteral(
            "CREATE TABLE tracks AS SELECT * FROM read_parquet('%1')")
            .arg(Impl::sqlQuote(d->parquetPath));
        if (!d->exec(load)) d->exec(QString::fromLatin1(kSchema));
    } else {
        d->exec(QString::fromLatin1(kSchema));
    }
    d->open = true;
    return true;
}

int MediaLibraryIndex::rescan(const QString &root) {
    if (!isOpen() || root.isEmpty()) return 0;

    d->exec(QStringLiteral("DROP TABLE IF EXISTS tracks"));
    d->exec(QStringLiteral(
        "CREATE TABLE tracks("
        "path VARCHAR, artist VARCHAR, albumartist VARCHAR, album VARCHAR, "
        "title VARCHAR, tracknum INTEGER, disc INTEGER, year INTEGER, "
        "genre VARCHAR, length_ms BIGINT)"));

    duckdb_appender appender = nullptr;
    if (duckdb_appender_create(d->con, nullptr, "tracks", &appender)
        != DuckDBSuccess)
        return 0;

    int count = 0;
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo fi(path);
        if (!audioExts().contains(fi.suffix().toLower())) continue;

        audiometa::Tags t = audiometa::tags(path);
        // Filename / folder fallbacks for files that carry no tags, so a
        // "Artist - Title.ext" collection still groups sensibly.
        if (t.artist.isEmpty() || t.title.isEmpty()) {
            const QString base = fi.completeBaseName();
            const int dash = base.indexOf(QStringLiteral(" - "));
            if (dash > 0) {
                if (t.artist.isEmpty()) t.artist = base.left(dash).trimmed();
                if (t.title.isEmpty())  t.title  = base.mid(dash + 3).trimmed();
            } else if (t.title.isEmpty()) {
                t.title = base;
            }
        }
        if (t.album.isEmpty()) t.album = fi.dir().dirName();

        const QByteArray bPath  = path.toUtf8();
        const QByteArray bArt   = t.artist.toUtf8();
        const QByteArray bAlbA  = t.albumArtist.toUtf8();
        const QByteArray bAlb   = t.album.toUtf8();
        const QByteArray bTitle = t.title.toUtf8();
        const QByteArray bGenre = t.genre.toUtf8();
        duckdb_append_varchar(appender, bPath.constData());
        duckdb_append_varchar(appender, bArt.constData());
        duckdb_append_varchar(appender, bAlbA.constData());
        duckdb_append_varchar(appender, bAlb.constData());
        duckdb_append_varchar(appender, bTitle.constData());
        duckdb_append_int32(appender, t.track < 0 ? 0 : t.track);
        duckdb_append_int32(appender, t.disc  < 0 ? 0 : t.disc);
        duckdb_append_int32(appender, t.year  < 0 ? 0 : t.year);
        duckdb_append_varchar(appender, bGenre.constData());
        duckdb_append_int64(appender, t.lengthMs < 0 ? 0 : t.lengthMs);
        duckdb_appender_end_row(appender);
        ++count;
    }
    duckdb_appender_close(appender);
    duckdb_appender_destroy(&appender);

    // Persist the snapshot so the next launch loads instantly.
    d->exec(QStringLiteral("COPY tracks TO '%1' (FORMAT PARQUET)")
                .arg(Impl::sqlQuote(d->parquetPath)));
    return count;
}

// The artist dimension: album-artist when present, else track-artist,
// with featured-guest suffixes stripped so "Eminem/ Jessie Reyez" and
// "Eminem feat. X" collapse under "Eminem".  The "/ " (slash + space)
// form is what tags use for guests; a bare "/" (as in "AC/DC") is left
// intact.
static const char *kEA =
    "TRIM(regexp_replace("
    "CASE WHEN albumartist <> '' THEN albumartist ELSE artist END,"
    "'(\\s*/\\s+|\\s+(feat\\.?|ft\\.?|featuring|with)\\s+).*$', '', 'i'))";

QList<MlArtist> MediaLibraryIndex::artists() const {
    QList<MlArtist> out;
    if (!isOpen()) return out;
    const QString sql = QStringLiteral(
        "SELECT ea, COUNT(DISTINCT album), COUNT(*) FROM "
        "(SELECT %1 AS ea, album FROM tracks) "
        "GROUP BY ea ORDER BY lower(ea)").arg(QString::fromLatin1(kEA));
    duckdb_result r;
    if (duckdb_query(d->con, sql.toUtf8().constData(), &r) != DuckDBSuccess)
        return out;
    const idx_t rows = duckdb_row_count(&r);
    for (idx_t i = 0; i < rows; ++i) {
        MlArtist a;
        a.name       = Impl::colStr(&r, 0, i);
        a.albumCount = int(duckdb_value_int64(&r, 1, i));
        a.trackCount = int(duckdb_value_int64(&r, 2, i));
        out.append(a);
    }
    duckdb_destroy_result(&r);
    return out;
}

QList<MlAlbum> MediaLibraryIndex::albums(const QString &artist) const {
    QList<MlAlbum> out;
    if (!isOpen()) return out;
    QString sql = QStringLiteral(
        "SELECT album, MAX(year), COUNT(*) FROM tracks ");
    if (!artist.isEmpty())
        sql += QStringLiteral("WHERE %1 = ? ").arg(QString::fromLatin1(kEA));
    sql += QStringLiteral("GROUP BY album ORDER BY MAX(year), lower(album)");

    duckdb_prepared_statement stmt = nullptr;
    if (duckdb_prepare(d->con, sql.toUtf8().constData(), &stmt) != DuckDBSuccess) {
        duckdb_destroy_prepare(&stmt);
        return out;
    }
    if (!artist.isEmpty())
        duckdb_bind_varchar(stmt, 1, artist.toUtf8().constData());
    duckdb_result r;
    if (duckdb_execute_prepared(stmt, &r) == DuckDBSuccess) {
        const idx_t rows = duckdb_row_count(&r);
        for (idx_t i = 0; i < rows; ++i) {
            MlAlbum a;
            a.name       = Impl::colStr(&r, 0, i);
            a.year       = int(duckdb_value_int64(&r, 1, i));
            a.trackCount = int(duckdb_value_int64(&r, 2, i));
            out.append(a);
        }
        duckdb_destroy_result(&r);
    }
    duckdb_destroy_prepare(&stmt);
    return out;
}

QList<MlTrack> MediaLibraryIndex::tracks(const QString &artist,
                                          const QString &album) const {
    QList<MlTrack> out;
    if (!isOpen()) return out;
    QString sql = QStringLiteral(
        "SELECT artist, album, title, genre, tracknum, year, length_ms, path "
        "FROM tracks WHERE 1=1 ");
    if (!artist.isEmpty())
        sql += QStringLiteral("AND %1 = ? ").arg(QString::fromLatin1(kEA));
    if (!album.isEmpty())
        sql += QStringLiteral("AND album = ? ");
    sql += QStringLiteral("ORDER BY disc, tracknum, lower(title)");

    duckdb_prepared_statement stmt = nullptr;
    if (duckdb_prepare(d->con, sql.toUtf8().constData(), &stmt) != DuckDBSuccess) {
        duckdb_destroy_prepare(&stmt);
        return out;
    }
    idx_t p = 1;
    if (!artist.isEmpty())
        duckdb_bind_varchar(stmt, p++, artist.toUtf8().constData());
    if (!album.isEmpty())
        duckdb_bind_varchar(stmt, p++, album.toUtf8().constData());
    duckdb_result r;
    if (duckdb_execute_prepared(stmt, &r) == DuckDBSuccess) {
        const idx_t rows = duckdb_row_count(&r);
        for (idx_t i = 0; i < rows; ++i) {
            MlTrack t;
            t.artist   = Impl::colStr(&r, 0, i);
            t.album    = Impl::colStr(&r, 1, i);
            t.title    = Impl::colStr(&r, 2, i);
            t.genre    = Impl::colStr(&r, 3, i);
            t.track    = int(duckdb_value_int64(&r, 4, i));
            t.year     = int(duckdb_value_int64(&r, 5, i));
            t.lengthMs = duckdb_value_int64(&r, 6, i);
            t.path     = Impl::colStr(&r, 7, i);
            out.append(t);
        }
        duckdb_destroy_result(&r);
    }
    duckdb_destroy_prepare(&stmt);
    return out;
}

int MediaLibraryIndex::totalTracks() const {
    if (!isOpen()) return 0;
    duckdb_result r;
    if (duckdb_query(d->con, "SELECT COUNT(*) FROM tracks", &r) != DuckDBSuccess)
        return 0;
    int n = duckdb_row_count(&r) > 0 ? int(duckdb_value_int64(&r, 0, 0)) : 0;
    duckdb_destroy_result(&r);
    return n;
}

#else  // !QTAMP_HAVE_DUCKDB — no-op index (empty library).

struct MediaLibraryIndex::Impl {};
MediaLibraryIndex::MediaLibraryIndex() = default;
MediaLibraryIndex::~MediaLibraryIndex() = default;
bool MediaLibraryIndex::open(const QString &) { return false; }
bool MediaLibraryIndex::isOpen() const { return false; }
int  MediaLibraryIndex::rescan(const QString &) { return 0; }
QList<MlArtist> MediaLibraryIndex::artists() const { return {}; }
QList<MlAlbum>  MediaLibraryIndex::albums(const QString &) const { return {}; }
QList<MlTrack>  MediaLibraryIndex::tracks(const QString &, const QString &) const { return {}; }
int  MediaLibraryIndex::totalTracks() const { return 0; }

#endif

}  // namespace qtamp
