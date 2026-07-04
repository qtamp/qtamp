// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "medialibraryindex.h"

#include "audiometa.h"

#include <QDateTime>
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
    QString           statsPath;
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
    d->statsPath   = QDir(cacheDir).filePath(QStringLiteral("stats.parquet"));

    static const char *kSchema =
        "CREATE TABLE tracks("
        "path VARCHAR, artist VARCHAR, albumartist VARCHAR, album VARCHAR, "
        "title VARCHAR, tracknum INTEGER, disc INTEGER, year INTEGER, "
        "genre VARCHAR, length_ms BIGINT, dateadded BIGINT)";

    if (QFileInfo::exists(d->parquetPath)) {
        // Load the persisted snapshot into a fresh table.
        const QString load = QStringLiteral(
            "CREATE TABLE tracks AS SELECT * FROM read_parquet('%1')")
            .arg(Impl::sqlQuote(d->parquetPath));
        if (!d->exec(load)) d->exec(QString::fromLatin1(kSchema));
        // Older snapshots predate the dateadded column; patch it in so
        // every query below can rely on it.
        if (!d->exec(QStringLiteral("SELECT dateadded FROM tracks LIMIT 0")))
            d->exec(QStringLiteral(
                "ALTER TABLE tracks ADD COLUMN dateadded BIGINT DEFAULT 0"));
    } else {
        d->exec(QString::fromLatin1(kSchema));
    }

    // Play-stats sidecar (playcount / lastplay / rating per path) —
    // ml_local keeps these in its NDE db; a separate Parquet keeps the
    // track snapshot immutable across plays.
    static const char *kStatsSchema =
        "CREATE TABLE stats("
        "path VARCHAR, playcount INTEGER, lastplay BIGINT, rating INTEGER)";
    if (QFileInfo::exists(d->statsPath)) {
        const QString load = QStringLiteral(
            "CREATE TABLE stats AS SELECT * FROM read_parquet('%1')")
            .arg(Impl::sqlQuote(d->statsPath));
        if (!d->exec(load)) d->exec(QString::fromLatin1(kStatsSchema));
    } else {
        d->exec(QString::fromLatin1(kStatsSchema));
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
        "genre VARCHAR, length_ms BIGINT, dateadded BIGINT)"));

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
        // dateadded: the file's mtime stands in for ml_local's
        // import-time stamp (a fresh rescan should not mark the whole
        // collection "Recently Added").
        duckdb_append_int64(appender,
                            fi.lastModified().toSecsSinceEpoch());
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

// SQL expression for a filter field.  "artist" gets the album-artist
// fold + guest strip; "year" is projected as text so pane values and
// equality filters share one representation.
static QString fieldExpr(const QString &field) {
    if (field == QStringLiteral("artist"))
        return QString::fromLatin1(kEA);
    if (field == QStringLiteral("albumartist"))
        return QStringLiteral(
            "CASE WHEN albumartist <> '' THEN albumartist ELSE artist END");
    if (field == QStringLiteral("year"))
        return QStringLiteral("CAST(year AS VARCHAR)");
    if (field == QStringLiteral("album") || field == QStringLiteral("genre"))
        return field;
    return QStringLiteral("''");   // unknown field → empty dimension
}

// WHERE fragment for the upstream pane selections, with values bound
// through prepared-statement parameters by the callers below.
static QString equalsWhere(const QList<QPair<QString, QString>> &equals) {
    QString w;
    for (const auto &e : equals)
        w += QStringLiteral("AND %1 = ? ").arg(fieldExpr(e.first));
    return w;
}

QList<MediaLibraryIndex::MlFilterValue> MediaLibraryIndex::filterValues(
    const QString &field, const QString &countField,
    const QList<QPair<QString, QString>> &equals) const {
    QList<MlFilterValue> out;
    if (!isOpen()) return out;
    const QString count = countField.isEmpty()
        ? QStringLiteral("COUNT(*)")
        : QStringLiteral("COUNT(DISTINCT %1)").arg(fieldExpr(countField));
    QString sql = QStringLiteral(
        "SELECT %1 AS v, %2 FROM tracks WHERE 1=1 %3GROUP BY v ORDER BY %4")
        .arg(fieldExpr(field), count, equalsWhere(equals),
             field == QStringLiteral("year")
                 ? QStringLiteral("TRY_CAST(v AS INTEGER)")
                 : QStringLiteral("lower(v)"));
    duckdb_prepared_statement stmt = nullptr;
    if (duckdb_prepare(d->con, sql.toUtf8().constData(), &stmt)
        != DuckDBSuccess) {
        duckdb_destroy_prepare(&stmt);
        return out;
    }
    idx_t p = 1;
    for (const auto &e : equals)
        duckdb_bind_varchar(stmt, p++, e.second.toUtf8().constData());
    duckdb_result r;
    if (duckdb_execute_prepared(stmt, &r) == DuckDBSuccess) {
        const idx_t rows = duckdb_row_count(&r);
        for (idx_t i = 0; i < rows; ++i) {
            MlFilterValue v;
            v.name  = Impl::colStr(&r, 0, i);
            v.count = int(duckdb_value_int64(&r, 1, i));
            out.append(v);
        }
        duckdb_destroy_result(&r);
    }
    duckdb_destroy_prepare(&stmt);
    return out;
}

QList<MlTrack> MediaLibraryIndex::tracksQuery(
    const QList<QPair<QString, QString>> &equals, int smartView) const {
    QList<MlTrack> out;
    if (!isOpen()) return out;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    // ml_local's stock smart-view queries, joined against the stats
    // sidecar.  The video view keeps its `type = 1` meaning: the index
    // holds audio only, so it is honestly empty until video indexing
    // lands.
    QString where, order = QStringLiteral("t.disc, t.tracknum, lower(t.title)");
    switch (smartView) {
    case 1: where = QStringLiteral("AND 1=0 ");                        break;
    case 2: where = QStringLiteral("AND COALESCE(s.playcount,0) > 0 ");
            order = QStringLiteral("s.playcount DESC");                break;
    case 3: where = QStringLiteral("AND t.dateadded > %1 ")
                        .arg(now - qint64(3) * 24 * 3600);
            order = QStringLiteral("t.dateadded DESC");                break;
    case 4: where = QStringLiteral("AND COALESCE(s.lastplay,0) > %1 ")
                        .arg(now - qint64(14) * 24 * 3600);
            order = QStringLiteral("s.lastplay DESC");                 break;
    case 5: where = QStringLiteral("AND COALESCE(s.playcount,0) = 0 "); break;
    case 6: where = QStringLiteral("AND COALESCE(s.rating,0) >= 3 ");   break;
    default: break;
    }
    // equalsWhere emits unqualified column expressions; the only
    // ambiguous name across the join is `path`, which no filter field
    // references, so they resolve against `tracks` as intended.
    QString sql = QStringLiteral(
        "SELECT t.artist, t.album, t.title, t.genre, t.tracknum, t.year, "
        "t.length_ms, t.path FROM tracks t "
        "LEFT JOIN stats s ON s.path = t.path WHERE 1=1 %1%2ORDER BY %3")
        .arg(where, equalsWhere(equals), order);
    duckdb_prepared_statement stmt = nullptr;
    if (duckdb_prepare(d->con, sql.toUtf8().constData(), &stmt)
        != DuckDBSuccess) {
        duckdb_destroy_prepare(&stmt);
        return out;
    }
    idx_t p = 1;
    for (const auto &e : equals)
        duckdb_bind_varchar(stmt, p++, e.second.toUtf8().constData());
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

void MediaLibraryIndex::recordPlay(const QString &path) {
    if (!isOpen() || path.isEmpty()) return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QString q = Impl::sqlQuote(path);
    // Upsert: bump an existing row, else insert the first play.
    duckdb_result r;
    bool updated = false;
    const QString upd = QStringLiteral(
        "UPDATE stats SET playcount = playcount + 1, lastplay = %1 "
        "WHERE path = '%2'").arg(now).arg(q);
    if (duckdb_query(d->con, upd.toUtf8().constData(), &r) == DuckDBSuccess) {
        updated = duckdb_rows_changed(&r) > 0;
        duckdb_destroy_result(&r);
    }
    if (!updated)
        d->exec(QStringLiteral(
            "INSERT INTO stats VALUES ('%1', 1, %2, 0)").arg(q).arg(now));
    d->exec(QStringLiteral("COPY stats TO '%1' (FORMAT PARQUET)")
                .arg(Impl::sqlQuote(d->statsPath)));
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
QList<MediaLibraryIndex::MlFilterValue> MediaLibraryIndex::filterValues(
    const QString &, const QString &,
    const QList<QPair<QString, QString>> &) const { return {}; }
QList<MlTrack> MediaLibraryIndex::tracksQuery(
    const QList<QPair<QString, QString>> &, int) const { return {}; }
void MediaLibraryIndex::recordPlay(const QString &) {}

#endif

}  // namespace qtamp
