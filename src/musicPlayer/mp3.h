#ifndef MUSICPLAYER_MP3_H__
#define MUSICPLAYER_MP3_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "utils/log.h"

/**
 * Track duration for MP3 files.
 *
 * SDL_mixer 1.2 cannot report either the duration or the current play position
 * of a Mix_Music, so a progress bar needs the duration computed here and the
 * elapsed time tracked by the caller.
 *
 * Strategy, in order of preference:
 *   1. A Xing/Info/VBRI header, which stores the exact frame count.
 *   2. Walking every frame header and counting.
 *
 * A single-frame CBR extrapolation is deliberately NOT used as a general case:
 * the bundled Onion theme tracks are VBR with no Xing header, and extrapolating
 * from the first frame's bitrate gives ~154s for a 205s file.
 */

// [version][bitrate_index], kbps. version: 0 = MPEG1, 1 = MPEG2/2.5. Layer III.
static const int MP3_BITRATES[2][16] = {
    {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0},
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0}};

// [mpeg_version_bits][samplerate_index]. Version bits: 0 = MPEG2.5, 2 = MPEG2, 3 = MPEG1.
static const int MP3_SAMPLERATES[4][4] = {
    {11025, 12000, 8000, 0}, // MPEG 2.5
    {0, 0, 0, 0},            // reserved
    {22050, 24000, 16000, 0},// MPEG 2
    {44100, 48000, 32000, 0} // MPEG 1
};

typedef struct Mp3Frame {
    int bitrate;      // bps
    int samplerate;   // Hz
    int samples;      // samples per frame
    int length;       // frame length in bytes, including padding
    int channels;
    bool is_mpeg1;
} Mp3Frame;

/** Parses a 4-byte frame header. Returns false if it isn't a valid Layer III frame. */
static bool _mp3_parseHeader(const uint8_t *h, Mp3Frame *out)
{
    // Sync word: 11 bits set.
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0)
        return false;

    int version_bits = (h[1] >> 3) & 0x03; // 0=2.5, 1=reserved, 2=2, 3=1
    int layer_bits = (h[1] >> 1) & 0x03;   // 1 = Layer III
    int bitrate_index = (h[2] >> 4) & 0x0F;
    int samplerate_index = (h[2] >> 2) & 0x03;
    int padding = (h[2] >> 1) & 0x01;
    int channel_mode = (h[3] >> 6) & 0x03; // 3 = mono

    if (version_bits == 1 || layer_bits != 1)
        return false;
    if (bitrate_index == 0 || bitrate_index == 15 || samplerate_index == 3)
        return false;

    bool is_mpeg1 = (version_bits == 3);

    out->is_mpeg1 = is_mpeg1;
    out->bitrate = MP3_BITRATES[is_mpeg1 ? 0 : 1][bitrate_index] * 1000;
    out->samplerate = MP3_SAMPLERATES[version_bits][samplerate_index];
    out->channels = (channel_mode == 3) ? 1 : 2;

    if (out->bitrate == 0 || out->samplerate == 0)
        return false;

    // Layer III: 1152 samples per frame on MPEG1, 576 on MPEG2/2.5.
    out->samples = is_mpeg1 ? 1152 : 576;

    // Frame length in bytes. The 144/72 constants are samples/8 for each case.
    int coefficient = is_mpeg1 ? 144 : 72;
    out->length = (coefficient * out->bitrate) / out->samplerate + padding;

    return out->length > 4;
}

/** Returns the byte offset of the audio data, skipping any ID3v2 tag. */
static long _mp3_skipId3(FILE *fp)
{
    uint8_t header[10];

    if (fread(header, 1, 10, fp) != 10)
        return 0;

    if (memcmp(header, "ID3", 3) != 0)
        return 0;

    // Size is 4 syncsafe bytes: 7 significant bits each.
    long size = ((long)(header[6] & 0x7F) << 21) |
                ((long)(header[7] & 0x7F) << 14) |
                ((long)(header[8] & 0x7F) << 7) | (long)(header[9] & 0x7F);

    // Bit 4 of the flags marks a 10-byte footer.
    if (header[5] & 0x10)
        size += 10;

    return size + 10;
}

/**
 * Reads the frame count from a Xing/Info or VBRI header, if present.
 * `frame_start` must be the offset of the first frame. Returns 0 if absent.
 */
static uint32_t _mp3_vbrFrameCount(FILE *fp, long frame_start, const Mp3Frame *first)
{
    uint8_t buf[4];

    // Xing/Info sits after the side information, whose size depends on version
    // and channel count.
    int side_info;
    if (first->is_mpeg1)
        side_info = (first->channels == 1) ? 17 : 32;
    else
        side_info = (first->channels == 1) ? 9 : 17;

    if (fseek(fp, frame_start + 4 + side_info, SEEK_SET) != 0)
        return 0;
    if (fread(buf, 1, 4, fp) != 4)
        return 0;

    if (memcmp(buf, "Xing", 4) == 0 || memcmp(buf, "Info", 4) == 0) {
        uint8_t flags[4];
        if (fread(flags, 1, 4, fp) != 4)
            return 0;
        // Bit 0 of the flags means a frame count follows.
        if (!(flags[3] & 0x01))
            return 0;
        if (fread(buf, 1, 4, fp) != 4)
            return 0;
        return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
               ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
    }

    // VBRI is always 32 bytes past the frame header.
    if (fseek(fp, frame_start + 4 + 32, SEEK_SET) != 0)
        return 0;
    if (fread(buf, 1, 4, fp) != 4)
        return 0;
    if (memcmp(buf, "VBRI", 4) != 0)
        return 0;
    if (fseek(fp, frame_start + 4 + 32 + 14, SEEK_SET) != 0)
        return 0;
    if (fread(buf, 1, 4, fp) != 4)
        return 0;
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

/**
 * Returns the duration of an MP3 in seconds, or 0.0 if it can't be determined.
 */
double mp3_getDuration(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        printf_debug("mp3_getDuration: cannot open %s\n", path);
        return 0.0;
    }

    long offset = _mp3_skipId3(fp);

    // Find the first frame. Some files have padding or garbage after the tag,
    // so scan forward a bounded distance rather than trusting the offset.
    Mp3Frame first;
    uint8_t header[4];
    bool found = false;
    const long max_scan = offset + 65536;

    for (long pos = offset; pos < max_scan; pos++) {
        if (fseek(fp, pos, SEEK_SET) != 0)
            break;
        if (fread(header, 1, 4, fp) != 4)
            break;
        if (_mp3_parseHeader(header, &first)) {
            offset = pos;
            found = true;
            break;
        }
    }

    if (!found) {
        printf_debug("mp3_getDuration: no frame header in %s\n", path);
        fclose(fp);
        return 0.0;
    }

    // Preferred: exact frame count from a VBR header.
    uint32_t vbr_frames = _mp3_vbrFrameCount(fp, offset, &first);
    if (vbr_frames > 0) {
        double duration = (double)vbr_frames * first.samples / first.samplerate;
        fclose(fp);
        return duration;
    }

    // Fallback: walk the frames. Handles VBR correctly, unlike extrapolating
    // from the first frame's bitrate.
    long pos = offset;
    uint32_t frames = 0;
    Mp3Frame frame;

    while (true) {
        if (fseek(fp, pos, SEEK_SET) != 0)
            break;
        if (fread(header, 1, 4, fp) != 4)
            break;
        if (!_mp3_parseHeader(header, &frame))
            break; // End of audio data (ID3v1 trailer, or corruption).
        frames++;
        pos += frame.length;
    }

    fclose(fp);

    if (frames == 0)
        return 0.0;

    return (double)frames * first.samples / first.samplerate;
}

#endif // MUSICPLAYER_MP3_H__
