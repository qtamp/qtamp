// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "audiometa.h"

#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <climits>

namespace audiometa {
namespace {

// First run of decimal digits anywhere in `s` → int, or -1.  Handles
// "3", "3/12" (→ 3), and ID3 text frames that carry a stray encoding
// byte or a UTF-16 BOM ahead of the number.
int firstInt(const QString &s) {
    int i = 0;
    const int n = s.size();
    while (i < n && !s.at(i).isDigit()) ++i;
    if (i >= n) return -1;
    int v = 0;
    while (i < n && s.at(i).isDigit()) {
        v = v * 10 + s.at(i).digitValue();
        ++i;
    }
    return v;
}

quint32 le32(const QByteArray &b, int p) {
    return quint32(quint8(b[p]))
         | (quint32(quint8(b[p + 1])) << 8)
         | (quint32(quint8(b[p + 2])) << 16)
         | (quint32(quint8(b[p + 3])) << 24);
}

// Parse a raw VORBIS_COMMENT payload (the block body, after any
// container framing): vendor string then a list of "KEY=value" entries,
// every length a little-endian u32.  Shared by FLAC and Ogg/Opus.
void parseVorbisComment(const QByteArray &b, Tags &out) {
    int p = 0;
    if (p + 4 > b.size()) return;
    const quint32 vlen = le32(b, p);
    p += 4 + int(vlen);
    if (p + 4 > b.size()) return;
    const quint32 count = le32(b, p);
    p += 4;
    for (quint32 i = 0; i < count; ++i) {
        if (p + 4 > b.size()) break;
        const quint32 clen = le32(b, p);
        p += 4;
        if (clen > quint32(b.size() - p)) break;
        const QByteArray c = b.mid(p, int(clen));
        p += int(clen);
        const int eq = c.indexOf('=');
        if (eq < 0) continue;
        const QString key = QString::fromUtf8(c.left(eq)).toUpper();
        const QString val = QString::fromUtf8(c.mid(eq + 1));
        if (key == QLatin1String("TRACKNUMBER"))          out.track = firstInt(val);
        else if (key == QLatin1String("DISCNUMBER"))      out.disc  = firstInt(val);
        else if (key == QLatin1String("ALBUM"))           out.album = val.trimmed();
        else if (key == QLatin1String("ARTIST"))          out.artist = val.trimmed();
        else if (key == QLatin1String("ALBUMARTIST") ||
                 key == QLatin1String("ALBUM ARTIST"))    out.albumArtist = val.trimmed();
        else if (key == QLatin1String("TITLE"))           out.title = val.trimmed();
        else if (key == QLatin1String("GENRE"))           out.genre = val.trimmed();
        else if (key == QLatin1String("DATE") ||
                 key == QLatin1String("YEAR"))            out.year  = firstInt(val);
    }
}

// FLAC: "fLaC" then a chain of metadata blocks (1-byte type+last flag,
// 3-byte big-endian length).  Block type 0 is STREAMINFO (→ duration),
// type 4 is VORBIS_COMMENT (→ text tags).
Tags parseFlac(QFile &f) {
    Tags out;
    if (!f.seek(4)) return out;
    for (;;) {
        const QByteArray hdr = f.read(4);
        if (hdr.size() != 4) break;
        const quint8  type = quint8(hdr[0]) & 0x7F;
        const bool    last = quint8(hdr[0]) & 0x80;
        const quint32 len  = (quint32(quint8(hdr[1])) << 16)
                           | (quint32(quint8(hdr[2])) << 8)
                           | quint32(quint8(hdr[3]));
        if (type == 0) {                              // STREAMINFO
            const QByteArray si = f.read(int(len));
            // Bits after the min/max block+frame sizes (10 bytes): a
            // 64-bit field of sampleRate(20) | channels(3) | bps(5) |
            // totalSamples(36), big-endian.
            if (si.size() >= 18) {
                const quint32 rate = (quint32(quint8(si[10])) << 12)
                                   | (quint32(quint8(si[11])) << 4)
                                   | (quint32(quint8(si[12])) >> 4);
                const quint64 samples =
                      (quint64(quint8(si[13]) & 0x0F) << 32)
                    | (quint64(quint8(si[14])) << 24)
                    | (quint64(quint8(si[15])) << 16)
                    | (quint64(quint8(si[16])) << 8)
                    |  quint64(quint8(si[17]));
                if (rate > 0 && samples > 0)
                    out.lengthMs = qint64(samples * 1000 / rate);
            }
        } else if (type == 4) {                       // VORBIS_COMMENT
            parseVorbisComment(f.read(int(len)), out);
        } else if (!f.seek(f.pos() + len)) {
            break;
        }
        if (last) break;
    }
    return out;
}

// Ogg (Vorbis/Opus): the comment header is stored verbatim, so locate
// the "vorbis"/"OpusTags" signature in the first pages and parse the
// VORBIS_COMMENT structure that follows.  Bounded read keeps it cheap.
Tags parseOgg(QFile &f) {
    Tags out;
    f.seek(0);
    const QByteArray buf = f.read(64 * 1024);
    int sig = buf.indexOf(QByteArray("\x03vorbis", 7));
    int off = sig >= 0 ? sig + 7 : -1;
    if (off < 0) {
        sig = buf.indexOf(QByteArray("OpusTags", 8));
        off = sig >= 0 ? sig + 8 : -1;
    }
    if (off >= 0 && off < buf.size())
        parseVorbisComment(buf.mid(off), out);
    return out;
}

// MP3: ID3v2 header ("ID3", 2 version bytes, flags, synchsafe size),
// then text frames.  TRCK = track, TPOS = disc, TALB = album (v2.3/4);
// TRK/TPA/TAL in the legacy v2.2 3-char ids.
Tags parseId3v2(QFile &f) {
    Tags out;
    f.seek(0);
    const QByteArray h = f.read(10);
    if (h.size() != 10 || h.left(3) != "ID3") return out;
    const quint8  ver  = quint8(h[3]);
    const quint32 size = (quint32(quint8(h[6])) << 21)
                       | (quint32(quint8(h[7])) << 14)
                       | (quint32(quint8(h[8])) << 7)
                       |  quint32(quint8(h[9]));
    const QByteArray tag = f.read(int(size));
    const bool v22   = (ver == 2);
    const int  idLen = v22 ? 3 : 4;
    const int  hdr   = v22 ? 6 : 10;

    auto decodeText = [](const QByteArray &body) -> QString {
        if (body.isEmpty()) return QString();
        const quint8 enc = quint8(body[0]);
        const QByteArray txt = body.mid(1);
        if (enc == 1 || enc == 2)            // UTF-16 (with/without BOM)
            return QString::fromUtf16(
                reinterpret_cast<const char16_t *>(txt.constData()),
                txt.size() / 2);
        if (enc == 3) return QString::fromUtf8(txt);   // UTF-8
        return QString::fromLatin1(txt);               // ISO-8859-1
    };

    int p = 0;
    while (p + hdr <= tag.size()) {
        const QByteArray id = tag.mid(p, idLen);
        if (id.isEmpty() || id.at(0) == '\0') break;   // padding
        quint32 fsize = 0;
        if (v22) {
            fsize = (quint32(quint8(tag[p + 3])) << 16)
                  | (quint32(quint8(tag[p + 4])) << 8)
                  |  quint32(quint8(tag[p + 5]));
        } else if (ver == 4) {                          // synchsafe
            fsize = (quint32(quint8(tag[p + 4])) << 21)
                  | (quint32(quint8(tag[p + 5])) << 14)
                  | (quint32(quint8(tag[p + 6])) << 7)
                  |  quint32(quint8(tag[p + 7]));
        } else {                                        // v2.3 plain
            fsize = (quint32(quint8(tag[p + 4])) << 24)
                  | (quint32(quint8(tag[p + 5])) << 16)
                  | (quint32(quint8(tag[p + 6])) << 8)
                  |  quint32(quint8(tag[p + 7]));
        }
        p += hdr;
        if (fsize == 0 || p + int(fsize) > tag.size()) break;
        const QByteArray body = tag.mid(p, int(fsize));
        p += int(fsize);
        const QString ids = QString::fromLatin1(id);
        if (ids == QLatin1String("TRCK") || ids == QLatin1String("TRK"))
            out.track = firstInt(decodeText(body));
        else if (ids == QLatin1String("TPOS") || ids == QLatin1String("TPA"))
            out.disc = firstInt(decodeText(body));
        else if (ids == QLatin1String("TALB") || ids == QLatin1String("TAL"))
            out.album = decodeText(body).trimmed();
        else if (ids == QLatin1String("TPE1") || ids == QLatin1String("TP1"))
            out.artist = decodeText(body).trimmed();
        else if (ids == QLatin1String("TPE2") || ids == QLatin1String("TP2"))
            out.albumArtist = decodeText(body).trimmed();
        else if (ids == QLatin1String("TIT2") || ids == QLatin1String("TT2"))
            out.title = decodeText(body).trimmed();
        else if (ids == QLatin1String("TCON") || ids == QLatin1String("TCO"))
            out.genre = decodeText(body).trimmed();
        else if (ids == QLatin1String("TYER") || ids == QLatin1String("TYE") ||
                 ids == QLatin1String("TDRC"))
            out.year = firstInt(decodeText(body));
        else if (ids == QLatin1String("TLEN"))
            out.lengthMs = firstInt(decodeText(body));
    }
    return out;
}

// MP4/M4A: the `trkn`/`disk` metadata items live under
// moov→udta→meta→ilst, each wrapping a `data` atom whose payload is
// 8 bytes of version/flags+reserved then 0x0000, the value, the total.
// A bounded byte-scan for the item id followed by its `data` atom is
// robust without walking the whole atom tree.
Tags parseMp4(QFile &f) {
    Tags out;
    f.seek(0);
    const QByteArray buf = f.read(1024 * 1024);   // metadata sits in moov, early

    auto u16be = [&](int p) -> int {
        if (p + 2 > buf.size()) return -1;
        return (quint8(buf[p]) << 8) | quint8(buf[p + 1]);
    };
    auto itemValue = [&](const char *id) -> int {
        int from = 0;
        for (;;) {
            const int idx = buf.indexOf(id, from);
            if (idx < 0) return -1;
            from = idx + 4;
            const int data = buf.indexOf("data", idx);
            // `data` atom must follow closely (its 8-byte atom header
            // sits right after the item's own 8-byte header).
            if (data < 0 || data - idx > 16) continue;
            // value word: data + 4(type) + 4(version/flags) + 4(reserved)
            //           + 2(leading pad) → the number, big-endian u16.
            const int v = u16be(data + 4 + 4 + 4 + 2);
            if (v >= 0) return v;
        }
    };
    auto textAtom = [&](const char *id) -> QString {
        const int idx = buf.indexOf(id, 0);
        if (idx < 0) return QString();
        const int data = buf.indexOf("data", idx);
        if (data < 0 || data - idx > 16) return QString();
        // atom size is the big-endian u32 at `data - 4` (the `data`
        // atom's own length field precedes its type).
        const int sizePos = data - 4;
        if (sizePos < 0) return QString();
        const quint32 atomLen = (quint32(quint8(buf[sizePos])) << 24)
                              | (quint32(quint8(buf[sizePos + 1])) << 16)
                              | (quint32(quint8(buf[sizePos + 2])) << 8)
                              |  quint32(quint8(buf[sizePos + 3]));
        const int textStart = data + 4 + 4 + 4;   // type+version+reserved
        const int textLen = int(atomLen) - 16;
        if (textLen <= 0 || textStart + textLen > buf.size()) return QString();
        return QString::fromUtf8(buf.mid(textStart, textLen)).trimmed();
    };

    out.track       = itemValue("trkn");
    out.disc        = itemValue("disk");
    out.album       = textAtom("\xA9""alb");
    out.artist      = textAtom("\xA9""ART");
    out.albumArtist = textAtom("aART");
    out.title       = textAtom("\xA9""nam");
    out.genre       = textAtom("\xA9""gen");
    out.year        = firstInt(textAtom("\xA9""day"));
    if (out.track == 0) out.track = -1;   // 0 means "absent" in trkn
    if (out.disc  == 0) out.disc  = -1;
    return out;
}

// A leading "01 - …" / "01." / "01 " number on the filename, for files
// that carry no tag at all.  -1 when the name doesn't start with digits.
int leadingFilenameNumber(const QString &path) {
    const QString name = QFileInfo(path).completeBaseName();
    int i = 0;
    while (i < name.size() && name.at(i).isDigit()) ++i;
    if (i == 0) return -1;
    return name.left(i).toInt();
}

}  // namespace

Tags tags(const QString &filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray magic = f.peek(12);
    if (magic.startsWith("fLaC"))                       return parseFlac(f);
    if (magic.startsWith("ID3"))                        return parseId3v2(f);
    if (magic.startsWith("OggS"))                       return parseOgg(f);
    if (magic.size() >= 8 && magic.mid(4, 4) == "ftyp") return parseMp4(f);
    // Fall back on the extension when the magic is inconclusive (e.g. an
    // MP3 with no ID3v2 header still has none of these tags to read).
    const QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == QLatin1String("flac"))                   return parseFlac(f);
    if (ext == QLatin1String("ogg") || ext == QLatin1String("opus"))
        return parseOgg(f);
    if (ext == QLatin1String("mp3"))                    return parseId3v2(f);
    if (ext == QLatin1String("m4a") || ext == QLatin1String("mp4") ||
        ext == QLatin1String("aac"))                    return parseMp4(f);
    return {};
}

Numbering numbering(const QString &filePath) {
    const Tags t = tags(filePath);
    return { t.track, t.disc, t.album };
}

void sortByTrack(QStringList &paths) {
    struct Keyed {
        QString album;     // lower-cased, for grouping
        int     disc;
        int     track;
        QString name;      // lower-cased filename, final tiebreak
        QString path;
    };
    QList<Keyed> keyed;
    keyed.reserve(paths.size());
    for (const QString &p : paths) {
        const Numbering n = numbering(p);
        int track = n.track;
        if (track < 0) track = leadingFilenameNumber(p);
        keyed.push_back({
            n.album.toLower(),
            n.disc < 0 ? 0 : n.disc,
            track < 0 ? INT_MAX : track,
            QFileInfo(p).fileName().toLower(),
            p });
    }
    std::stable_sort(keyed.begin(), keyed.end(),
        [](const Keyed &a, const Keyed &b) {
            if (a.album != b.album) return a.album < b.album;
            if (a.disc  != b.disc)  return a.disc  < b.disc;
            if (a.track != b.track) return a.track < b.track;
            return a.name < b.name;
        });
    paths.clear();
    for (const Keyed &k : keyed) paths << k.path;
}

}  // namespace audiometa
