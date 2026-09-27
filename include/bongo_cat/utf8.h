#ifndef BONGO_CAT_UTF8_H
#define BONGO_CAT_UTF8_H

#include <stdbool.h>
#include <stddef.h>

bool bongo_cat_utf8_valid(const char *text);
void bongo_cat_utf8_copy(char *output, size_t capacity, const char *text);
bool bongo_cat_utf8_normalize_mver(const char *text,
    char *output, size_t capacity);

#endif
