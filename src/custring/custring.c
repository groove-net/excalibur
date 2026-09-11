#include "../../include/custring.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The hidden base structure definition.
 * This matches the layout of FixedString8, FixedString256, etc.,
 * allowing safe casting via FSTR().
 */
struct FixedString_s {
  size_t length;
  size_t capacity;
  char data[];
};

static void fstr_overflow(size_t needed, size_t capacity) {
  fprintf(stderr,
          "FixedString error: buffer overflow (needed %zu bytes, capacity is "
          "%zu bytes)\n",
          needed, capacity);
  abort();
}

void fstr_assign_impl(FixedString *fstr, const char *src) {
  if (!fstr || !src)
    return;

  size_t src_len = strlen(src);
  // capacity in the struct represents total bytes allocated for data[N],
  // so max string length is capacity - 1 to leave room for '\0'.
  if (src_len >= fstr->capacity) {
    fstr_overflow(src_len + 1, fstr->capacity);
  }

  memcpy(fstr->data, src, src_len);
  fstr->data[src_len] = '\0';
  fstr->length = src_len;
}

void fstr_append_impl(FixedString *fstr, const char *src) {
  if (!fstr || !src)
    return;

  size_t src_len = strlen(src);
  size_t total_len = fstr->length + src_len;

  if (total_len >= fstr->capacity) {
    fstr_overflow(total_len + 1, fstr->capacity);
  }

  memcpy(fstr->data + fstr->length, src, src_len);
  fstr->length = total_len;
  fstr->data[fstr->length] = '\0';
}

void fstr_append_char_impl(FixedString *fstr, char c) {
  if (!fstr)
    return;

  if (fstr->length + 1 >= fstr->capacity) {
    fstr_overflow(fstr->length + 2, fstr->capacity);
  }

  fstr->data[fstr->length++] = c;
  fstr->data[fstr->length] = '\0';
}

void fstr_clear_impl(FixedString *fstr) {
  if (!fstr)
    return;
  fstr->length = 0;
  fstr->data[0] = '\0';
}
