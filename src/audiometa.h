#pragma once
//
// audiometa — a tiny, dependency-free, synchronous reader for the few
// tags qtamp needs to order an album folder by its album sequence:
// TRACKNUMBER, DISCNUMBER and ALBUM.  Qt's own metadata (QMediaPlayer)
// is asynchronous and therefore useless as a sort key, and qtamp links
// no TagLib, so we parse the handful of container formats directly:
//
//   * FLAC               — native metadata blocks → VORBIS_COMMENT
//   * Ogg Vorbis / Opus  — VORBIS_COMMENT text (stored verbatim)
//   * MP3                — ID3v2 (.2/.3/.4) TRCK / TPOS / TALB frames
//   * MP4 / M4A / AAC    — moov…ilst `trkn` / `disk` / `©alb` atoms
//
// Anything unrecognised yields the empty Numbering (track/disc = -1),
// which `sortByTrack` treats as "no tag" and falls back to filename.
//

#include <QString>
#include <QStringList>

namespace audiometa {

// Track/disc numbers and album title parsed from a file's tags.
// `track`/`disc` are -1 when absent; `album` is empty when absent.
struct Numbering {
    int     track = -1;
    int     disc  = -1;
    QString album;
};

// The full set of tags the Media Library index needs to build its
// artist → album → track rows.  Strings empty and ints -1 when absent;
// `lengthMs` is -1 unless the container exposes a duration cheaply
// (FLAC STREAMINFO; ID3v2 TLEN).  `year` is the leading 4-digit year
// of DATE / TYER / ©day.
struct Tags {
    QString artist;
    QString albumArtist;
    QString album;
    QString title;
    QString genre;
    int     track    = -1;
    int     disc     = -1;
    int     year     = -1;
    qint64  lengthMs = -1;
};

// Read all indexable tags from `filePath`.  Best-effort and synchronous;
// returns an empty Tags on any read/parse failure.
Tags tags(const QString &filePath);

// Read the numbering tags from `filePath`.  Best-effort and synchronous;
// returns an empty Numbering on any read/parse failure.
Numbering numbering(const QString &filePath);

// Order a list of audio file paths the way a real player presents an
// album: by (album, disc number, track number), with a case-insensitive
// filename tiebreak.  Files with no track tag fall back to a leading
// filename number ("01 - …"); files with neither sort last by filename,
// so a folder with no tags at all keeps today's plain alphabetical order.
void sortByTrack(QStringList &paths);

}  // namespace audiometa
