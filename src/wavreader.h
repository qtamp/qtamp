// wavreader.h — a tiny, dependency-free RIFF/WAVE PCM16 parser.
//
// The WebAssembly player has no QAudioDecoder (Qt's wasm multimedia
// backend reports it "Not available"), so it feeds its bundled demo
// track into the audio pipeline by parsing the WAV header directly.
// The parsing is pure byte-slicing with no Qt or platform dependency,
// which keeps it unit-testable on its own (see tests/wavreader_test.cpp)
// and identical across native and wasm builds.
#pragma once

#include <cstdint>
#include <cstddef>

namespace qtamp {

struct WavPcm {
    bool ok = false;          // header parsed and a non-empty data chunk found
    int channels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;    // only 16 is accepted by this reader
    const uint8_t *data = nullptr;  // points into the input buffer
    size_t dataLen = 0;             // length of the PCM data chunk in bytes

    long frames() const {
        const int bytesPerFrame = channels * (bitsPerSample / 8);
        return bytesPerFrame > 0 ? static_cast<long>(dataLen / bytesPerFrame) : 0;
    }
};

// Parse a little-endian 16-bit PCM WAV out of [buf, buf+size). On success
// returns ok=true with the format fields set and data/dataLen pointing at
// the PCM samples inside buf (no copy). Rejects non-RIFF/WAVE input, a
// missing or short fmt chunk, non-16-bit samples, and an empty data chunk.
inline WavPcm parseWavPcm16(const uint8_t *buf, size_t size) {
    WavPcm r;
    if (!buf || size < 12) return r;
    auto u32 = [&](size_t o) -> uint32_t {
        return uint32_t(buf[o]) | (uint32_t(buf[o + 1]) << 8)
             | (uint32_t(buf[o + 2]) << 16) | (uint32_t(buf[o + 3]) << 24);
    };
    auto u16 = [&](size_t o) -> uint16_t {
        return uint16_t(buf[o]) | (uint16_t(buf[o + 1]) << 8);
    };
    auto tag = [&](size_t o, const char *s) -> bool {
        return buf[o] == uint8_t(s[0]) && buf[o + 1] == uint8_t(s[1])
            && buf[o + 2] == uint8_t(s[2]) && buf[o + 3] == uint8_t(s[3]);
    };
    if (!tag(0, "RIFF") || !tag(8, "WAVE")) return r;

    size_t pos = 12;
    int channels = 0, rate = 0, bits = 0;
    while (pos + 8 <= size) {
        const uint32_t len = u32(pos + 4);
        const size_t body = pos + 8;
        if (tag(pos, "fmt ")) {
            if (len < 16 || body + 16 > size) return r;
            channels = u16(body + 2);
            rate = int(u32(body + 4));
            bits = u16(body + 14);
        } else if (tag(pos, "data")) {
            if (channels <= 0 || rate <= 0 || bits != 16) return r;
            size_t avail = size - body;
            r.dataLen = len <= avail ? len : avail;   // tolerate a short tail
            if (r.dataLen == 0) return r;
            r.data = buf + body;
            r.channels = channels;
            r.sampleRate = rate;
            r.bitsPerSample = bits;
            r.ok = r.frames() > 0;
            return r;
        }
        pos = body + len + (len & 1u);              // chunks are word-aligned
    }
    return r;
}

}  // namespace qtamp
