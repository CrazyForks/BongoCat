#ifndef BONGO_CAT_WINDOWS_LAYERED_PIXELS_H
#define BONGO_CAT_WINDOWS_LAYERED_PIXELS_H

#include <stddef.h>
#include <string.h>

#if defined(__SSE2__) || defined(_M_X64) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define BONGO_CAT_LAYERED_SSE2 1
#endif

/* Preserve BGRA byte-for-byte above the hit threshold, clearing all four
   channels below it. The DIB and resize buffers need no extra alignment. */
static inline void bongo_cat_windows_layered_filter_pixels(
    unsigned char *pixels, size_t count) {
#ifdef BONGO_CAT_LAYERED_SSE2
    const __m128i threshold = _mm_set1_epi32(8);
    while (count >= 4) {
        __m128i bgra = _mm_loadu_si128((const __m128i *)pixels);
        __m128i keep = _mm_cmpgt_epi32(_mm_srli_epi32(bgra, 24), threshold);
        if (_mm_movemask_epi8(keep) != 0xffff)
            _mm_storeu_si128((__m128i *)pixels, _mm_and_si128(bgra, keep));
        pixels += 16;
        count -= 4;
    }
#endif
    while (count--) {
        if (pixels[3] <= 8) memset(pixels, 0, 4);
        pixels += 4;
    }
}

#endif
