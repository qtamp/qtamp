#include "remotestate.h"

#include <QJsonArray>

namespace qtamp {

namespace {

QJsonObject transportToJson(const RemoteTransport &t) {
    return {{QStringLiteral("playing"), t.playing},
            {QStringLiteral("paused"), t.paused},
            {QStringLiteral("positionMs"), double(t.positionMs)},
            {QStringLiteral("positionAtMs"), double(t.positionAtMs)},
            {QStringLiteral("durationMs"), double(t.durationMs)},
            {QStringLiteral("volume"), t.volume},
            {QStringLiteral("pan"), t.pan}};
}

void transportFromJson(const QJsonObject &o, RemoteTransport *t) {
    t->playing = o.value(QLatin1String("playing")).toBool(t->playing);
    t->paused = o.value(QLatin1String("paused")).toBool(t->paused);
    t->positionMs =
        qint64(o.value(QLatin1String("positionMs")).toDouble(t->positionMs));
    t->positionAtMs = qint64(
        o.value(QLatin1String("positionAtMs")).toDouble(t->positionAtMs));
    t->durationMs =
        qint64(o.value(QLatin1String("durationMs")).toDouble(t->durationMs));
    t->volume = o.value(QLatin1String("volume")).toInt(t->volume);
    t->pan = o.value(QLatin1String("pan")).toDouble(t->pan);
}

QJsonObject trackToJson(const RemoteTrack &t) {
    return {{QStringLiteral("title"), t.title},
            {QStringLiteral("artist"), t.artist},
            {QStringLiteral("album"), t.album},
            {QStringLiteral("filename"), t.filename},
            {QStringLiteral("displayTitle"), t.displayTitle},
            {QStringLiteral("decoder"), t.decoder},
            {QStringLiteral("bitrate"), t.bitrate},
            {QStringLiteral("sampleRate"), t.sampleRate},
            {QStringLiteral("channels"), t.channels}};
}

void trackFromJson(const QJsonObject &o, RemoteTrack *t) {
    t->title = o.value(QLatin1String("title")).toString(t->title);
    t->artist = o.value(QLatin1String("artist")).toString(t->artist);
    t->album = o.value(QLatin1String("album")).toString(t->album);
    t->filename = o.value(QLatin1String("filename")).toString(t->filename);
    t->displayTitle =
        o.value(QLatin1String("displayTitle")).toString(t->displayTitle);
    t->decoder = o.value(QLatin1String("decoder")).toString(t->decoder);
    t->bitrate = o.value(QLatin1String("bitrate")).toInt(t->bitrate);
    t->sampleRate = o.value(QLatin1String("sampleRate")).toInt(t->sampleRate);
    t->channels = o.value(QLatin1String("channels")).toInt(t->channels);
}

QJsonObject playlistToJson(const RemotePlaylist &p) {
    QJsonArray rows;
    for (const RemotePlaylistRow &r : p.rows) {
        rows.append(QJsonObject{
            {QStringLiteral("text"), r.text},
            {QStringLiteral("durationMs"), double(r.durationMs)}});
    }
    return {{QStringLiteral("revision"), double(p.revision)},
            {QStringLiteral("count"), p.rows.size()},
            {QStringLiteral("currentIndex"), p.currentIndex},
            {QStringLiteral("rows"), rows}};
}

void playlistFromJson(const QJsonObject &o, RemotePlaylist *p) {
    p->revision =
        quint64(o.value(QLatin1String("revision")).toDouble(p->revision));
    p->currentIndex =
        o.value(QLatin1String("currentIndex")).toInt(p->currentIndex);
    const QJsonValue rowsVal = o.value(QLatin1String("rows"));
    if (rowsVal.isArray()) {
        p->rows.clear();
        const QJsonArray rows = rowsVal.toArray();
        p->rows.reserve(rows.size());
        for (const QJsonValue &v : rows) {
            const QJsonObject r = v.toObject();
            p->rows.append(
                {r.value(QLatin1String("text")).toString(),
                 qint64(r.value(QLatin1String("durationMs")).toDouble())});
        }
    }
}

QJsonObject eqToJson(const RemoteEq &e) {
    QJsonArray bands;
    for (int b : e.bands) bands.append(b);
    return {{QStringLiteral("on"), e.on},
            {QStringLiteral("auto"), e.autoOn},
            {QStringLiteral("preamp"), e.preamp},
            {QStringLiteral("bands"), bands}};
}

void eqFromJson(const QJsonObject &o, RemoteEq *e) {
    e->on = o.value(QLatin1String("on")).toBool(e->on);
    e->autoOn = o.value(QLatin1String("auto")).toBool(e->autoOn);
    e->preamp = o.value(QLatin1String("preamp")).toInt(e->preamp);
    const QJsonValue bandsVal = o.value(QLatin1String("bands"));
    if (bandsVal.isArray()) {
        e->bands.clear();
        for (const QJsonValue &v : bandsVal.toArray())
            e->bands.append(v.toInt(31));
        // The document is authoritative but the engine expects exactly 10
        // bands; pad/trim defensively.
        while (e->bands.size() < 10) e->bands.append(31);
        e->bands.resize(10);
    }
}

}  // namespace

bool parseSnapshot(const QJsonObject &doc, RemoteSnapshot *out) {
    if (!doc.value(QLatin1String("transport")).isObject() ||
        !doc.value(QLatin1String("playlist")).isObject()) {
        return false;
    }
    out->epoch = doc.value(QLatin1String("epoch")).toString();
    out->revision =
        quint64(doc.value(QLatin1String("revision")).toDouble(out->revision));
    out->serverNowMs = qint64(
        doc.value(QLatin1String("serverNowMs")).toDouble(out->serverNowMs));
    transportFromJson(doc.value(QLatin1String("transport")).toObject(),
                      &out->transport);
    if (doc.value(QLatin1String("track")).isObject())
        trackFromJson(doc.value(QLatin1String("track")).toObject(),
                      &out->track);
    playlistFromJson(doc.value(QLatin1String("playlist")).toObject(),
                     &out->playlist);
    if (doc.value(QLatin1String("eq")).isObject())
        eqFromJson(doc.value(QLatin1String("eq")).toObject(), &out->eq);
    return true;
}

QJsonObject serializeSnapshot(const RemoteSnapshot &s) {
    return {{QStringLiteral("epoch"), s.epoch},
            {QStringLiteral("revision"), double(s.revision)},
            {QStringLiteral("serverNowMs"), double(s.serverNowMs)},
            {QStringLiteral("transport"), transportToJson(s.transport)},
            {QStringLiteral("track"), trackToJson(s.track)},
            {QStringLiteral("playlist"), playlistToJson(s.playlist)},
            {QStringLiteral("eq"), eqToJson(s.eq)}};
}

ApplyResult applyEvent(const QByteArray &name, const QJsonObject &payload,
                       RemoteSnapshot *snap) {
    if (name == "ping") {
        snap->serverNowMs = qint64(payload.value(QLatin1String("serverNowMs"))
                                       .toDouble(snap->serverNowMs));
        return ApplyResult::Ignored;
    }
    if (name == "state") {
        // Full snapshot: the resync answer. Always authoritative.
        RemoteSnapshot fresh;
        if (!parseSnapshot(payload, &fresh)) return ApplyResult::Ignored;
        *snap = fresh;
        return ApplyResult::Applied;
    }

    // Sectional events carry {epoch?, revision, serverNowMs, <section>}.
    const QString epoch = payload.value(QLatin1String("epoch")).toString();
    if (!epoch.isEmpty() && !snap->epoch.isEmpty() && epoch != snap->epoch)
        return ApplyResult::NeedsResync;  // backend restarted underneath us
    const quint64 rev =
        quint64(payload.value(QLatin1String("revision")).toDouble());
    if (rev != 0 && rev <= snap->revision) return ApplyResult::Stale;
    const bool gap = rev != 0 && snap->revision != 0 &&
                     rev > snap->revision + 1;

    if (name == "transport" &&
        payload.value(QLatin1String("transport")).isObject()) {
        transportFromJson(payload.value(QLatin1String("transport")).toObject(),
                          &snap->transport);
    } else if (name == "track" &&
               payload.value(QLatin1String("track")).isObject()) {
        trackFromJson(payload.value(QLatin1String("track")).toObject(),
                      &snap->track);
    } else if (name == "playlist" &&
               payload.value(QLatin1String("playlist")).isObject()) {
        playlistFromJson(payload.value(QLatin1String("playlist")).toObject(),
                         &snap->playlist);
    } else if (name == "eq" && payload.value(QLatin1String("eq")).isObject()) {
        eqFromJson(payload.value(QLatin1String("eq")).toObject(), &snap->eq);
    } else {
        return ApplyResult::Ignored;
    }

    if (rev != 0) snap->revision = rev;
    snap->serverNowMs = qint64(payload.value(QLatin1String("serverNowMs"))
                                   .toDouble(snap->serverNowMs));
    // A revision gap means we missed at least one event (newest-wins
    // queues drop backlog by design): the section just applied is fresh,
    // but some other section may be stale — the caller re-snapshots.
    return gap ? ApplyResult::NeedsResync : ApplyResult::Applied;
}

}  // namespace qtamp
