#include "windows_layered_pixels.h"
#include "test.h"

int bongo_cat_test_failures;

int main(void) {
    unsigned char actual[16 + 257 * 4 + 16];
    unsigned char expected[sizeof(actual)];
    /* Exercise all alpha values, unaligned starts, vector tails, empty frames
       and guard bytes on both sides. Nonzero RGB below the threshold must also
       be cleared, while alpha 9 and above must preserve every channel. */
    for (size_t offset = 0; offset < 16; ++offset) {
        for (size_t count = 0; count <= 257; ++count) {
            memset(actual, 0xa5, sizeof(actual));
            for (size_t i = 0; i < count; ++i) {
                unsigned char *pixel = actual + offset + i * 4;
                pixel[0] = (unsigned char)(i * 17 + 1);
                pixel[1] = (unsigned char)(i * 31 + 2);
                pixel[2] = (unsigned char)(i * 47 + 3);
                pixel[3] = (unsigned char)i;
            }
            memcpy(expected, actual, sizeof(actual));
            for (size_t i = 0; i < count; ++i) {
                unsigned char *pixel = expected + offset + i * 4;
                if (pixel[3] <= 8) memset(pixel, 0, 4);
            }
            bongo_cat_windows_layered_filter_pixels(actual + offset, count);
            CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
        }
    }
    return bongo_cat_test_failures ? 1 : 0;
}
