/*
 * tronMSX — shared sound-chip mixer. B-MSX sound chip layer.
 *
 * PSG, SCC, YM2413, Y8950 and turboR PCM add into one int32 stereo block.
 * The last stage saturates to interleaved S16 at MSX_PCM_HZ. No float (D4),
 * no heap. The hosted window is the VirtIO-Sound client; this file only fills
 * the caller's buffer.
 */

#include "msx_int.h"

#include <string.h>

#define AUDIO_CHUNK 735u

static int16_t sat16(int32_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return (int16_t)-32768;
    return (int16_t)v;
}

uint32_t msx_audio_render(int16_t *out, uint32_t frames)
{
    uint32_t done = 0u;

    if (!out || !frames) return 0u;
    while (done < frames) {
        uint32_t n = frames - done;
        uint32_t i;
        int32_t acc[AUDIO_CHUNK * 2u];

        if (n > AUDIO_CHUNK) n = AUDIO_CHUNK;
        memset(acc, 0, (size_t)n * 2u * sizeof(int32_t));
        psg_mix(acc, n);
        scc_mix(acc, n);
        opll_mix(acc, n);
        y8950_mix(acc, n);
        pcm_mix(acc, n);
        for (i = 0u; i < n; i++) {
            out[(done + i) * 2u]     = sat16(acc[i * 2u]);
            out[(done + i) * 2u + 1u] = sat16(acc[i * 2u + 1u]);
        }
        done += n;
    }
    return done;
}
