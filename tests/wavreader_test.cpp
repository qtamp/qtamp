// Unit tests for src/wavreader.h, the dependency-free RIFF/WAVE PCM16
// parser that feeds the WebAssembly player's demo track into the audio
// pipeline (QAudioDecoder is unavailable in Qt's wasm backend). No Qt,
// no framework: a handful of assertions over hand-built byte buffers.

#include "wavreader.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using qtamp::parseWavPcm16;
using qtamp::WavPcm;

static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

namespace {

void put32(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back(x & 0xff); v.push_back((x >> 8) & 0xff);
    v.push_back((x >> 16) & 0xff); v.push_back((x >> 24) & 0xff);
}
void put16(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back(x & 0xff); v.push_back((x >> 8) & 0xff);
}
void tag(std::vector<uint8_t> &v, const char *s) {
    for (int i = 0; i < 4; ++i) v.push_back(uint8_t(s[i]));
}

// Build a valid PCM16 WAV with `frames` frames of `channels` at `rate`.
std::vector<uint8_t> makeWav(int channels, int rate, int frames,
                             int bits = 16, bool withJunk = false) {
    const int bytesPerSample = bits / 8;
    const uint32_t dataLen = uint32_t(frames * channels * bytesPerSample);
    std::vector<uint8_t> v;
    tag(v, "RIFF"); put32(v, 0); tag(v, "WAVE");
    if (withJunk) {                      // an ignorable chunk before fmt
        tag(v, "LIST"); put32(v, 4); tag(v, "INFO");
    }
    tag(v, "fmt "); put32(v, 16);
    put16(v, 1);                          // PCM
    put16(v, uint16_t(channels));
    put32(v, uint32_t(rate));
    put32(v, uint32_t(rate * channels * bytesPerSample));  // byte rate
    put16(v, uint16_t(channels * bytesPerSample));         // block align
    put16(v, uint16_t(bits));
    tag(v, "data"); put32(v, dataLen);
    for (uint32_t i = 0; i < dataLen; ++i) v.push_back(uint8_t(i & 0xff));
    return v;
}

WavPcm parse(const std::vector<uint8_t> &v) {
    return parseWavPcm16(v.data(), v.size());
}

}  // namespace

int main() {
    // Valid mono.
    {
        auto v = makeWav(1, 22050, 100);
        auto w = parse(v);
        CHECK(w.ok);
        CHECK(w.channels == 1);
        CHECK(w.sampleRate == 22050);
        CHECK(w.bitsPerSample == 16);
        CHECK(w.frames() == 100);
        CHECK(w.dataLen == 200);
    }
    // Valid stereo, different rate.
    {
        auto w = parse(makeWav(2, 44100, 50));
        CHECK(w.ok);
        CHECK(w.channels == 2);
        CHECK(w.sampleRate == 44100);
        CHECK(w.frames() == 50);         // 50 frames * 2ch * 2B = 200B
        CHECK(w.dataLen == 200);
    }
    // An unrelated chunk before fmt is skipped, not fatal.
    {
        auto w = parse(makeWav(1, 8000, 10, 16, /*withJunk=*/true));
        CHECK(w.ok);
        CHECK(w.sampleRate == 8000);
        CHECK(w.frames() == 10);
    }
    // data pointer actually addresses the samples in the buffer.
    {
        auto v = makeWav(1, 22050, 4);
        auto w = parse(v);
        CHECK(w.ok);
        CHECK(w.data >= v.data() && w.data + w.dataLen <= v.data() + v.size());
        CHECK(w.data[0] == 0x00 && w.data[1] == 0x01);  // first two sample bytes
    }
    // Not a RIFF file.
    {
        std::vector<uint8_t> junk = {'O','g','g','S', 0,0,0,0, 'x','x','x','x'};
        CHECK(!parse(junk).ok);
    }
    // RIFF but not WAVE.
    {
        std::vector<uint8_t> v;
        tag(v, "RIFF"); put32(v, 0); tag(v, "AVI ");
        CHECK(!parse(v).ok);
    }
    // 8-bit is rejected (reader is PCM16 only).
    {
        auto w = parse(makeWav(1, 22050, 100, /*bits=*/8));
        CHECK(!w.ok);
    }
    // Empty / too small buffers.
    {
        CHECK(!parseWavPcm16(nullptr, 0).ok);
        std::vector<uint8_t> tiny = {'R','I','F','F'};
        CHECK(!parse(tiny).ok);
    }
    // Header present but the data chunk is empty.
    {
        auto w = parse(makeWav(1, 22050, 0));
        CHECK(!w.ok);                    // zero frames is not playable
    }
    // Truncated data chunk (declared length exceeds the buffer): the reader
    // clamps to what is present instead of reading out of bounds.
    {
        auto v = makeWav(1, 22050, 100);
        v.resize(v.size() - 40);         // drop the last 40 bytes of PCM
        auto w = parse(v);
        CHECK(w.ok);
        CHECK(w.data + w.dataLen <= v.data() + v.size());
        CHECK(w.frames() == 80);         // 200 - 40 = 160 bytes = 80 frames
    }

    if (g_failures == 0) {
        std::printf("wavreader_test: all assertions passed\n");
        return 0;
    }
    std::printf("wavreader_test: %d failure(s)\n", g_failures);
    return 1;
}
