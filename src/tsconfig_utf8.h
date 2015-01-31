/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Copyright 2015, Tim Armstrong
 *
 * Author: Tim Armstrong <tim.g.armstrong@gmail.com>
 * Created: 20 July 2014
 */

/*
 * UTF-8 Functions.
 *
 * Validation rules based on RFC-3629
 */

#ifndef __TSCONFIG_UTF8_H
#define __TSCONFIG_UTF8_H

#include <stdint.h>

#include "tsconfig_common.h"

/*
 * Decoded unicode character.
 */
typedef uint32_t tscfg_char_t;

// Max bytes per encoded UTF-8 character
#define UTF8_MAX_BYTES 4

/*
 * Decode first byte of UTF-8 character.
 * This will detect any overlong encodings
 * accum: accumulator final value, store value of first byte
 * len: number of bytes in full encoding (including this one)
 * return: TSCFG_OK if valid initial byte of UTF-8, TSCFG_ERR_INVALID
 *        if invalid initial byte.
 */
static inline tscfg_rc tscfg_decode_byte1(unsigned char b, size_t *len,
                                         tscfg_char_t *accum);

/*
 * s: pointer to string
 * len: length of remainder to decode
 * accum: values added to accumulator
 * return: TSCFG_OK if valid bytes UTF-8, TSCFG_ERR_INVALID if invalid
 */
static inline tscfg_rc tscfg_decode_rest(const unsigned char *s, size_t len,
                                        tscfg_char_t *accum);

/*==============================*
 * Inline function definitions  *
 *==============================*/
static inline tscfg_rc tscfg_decode_byte1(unsigned char b, size_t *len,
                                         tscfg_char_t *accum) {
  if (b <= 0x7F)  { // Binary 0xxx xxxx
    *len = 1;
    *accum = 0x7F & b;
    return TSCFG_OK;
  } else if (b <= 0xC1) {
    /*
     * Detect continuation bytes <= 0xBF, i.e. binary 10xx xxxx and
     * overlong 2-byte encodings of 1-byte characters: 0xC0 and 0xC1,
     * i.e. binary 1100 000x.
     */
    return TSCFG_ERR_INVALID;
  } else if (b <= 0xDF) { // Binary 110x xxxx, except 1100 000x
    *len = 2;
    *accum = 0x1F & b;
    // All overlong 2-byte decodings already detected
    return TSCFG_OK;
  } else if (b <= 0xEF) { // Binary 1110 xxxx
    *len = 3;
    *accum = 0x0F & b;
  } else if (b <= 0xF7) { // Binary 1111 0xxx
    *len = 4;
    *accum = 0x07 & b;
  } else {
    // Invalid first byte
    return TSCFG_ERR_INVALID;
  }

  if (*accum == 0x0) {
    /*
     * Detect overlong encoding.
     * This handles all cases of overlong characters where it was extended
     * by > 1 byte, or where the canonical encoding is longer than 1 byte.
     * This leaves 2 byte encodings of 1 byte characters, which is handled
     * above.
     */
    return TSCFG_ERR_INVALID;
  }
  return TSCFG_OK;
}

static inline tscfg_rc tscfg_decode_rest(const unsigned char *s, size_t len,
                                        tscfg_char_t *accum) {
  for (size_t i = 0; i < len; i++) {
    unsigned char b = s[i];
    // Must follow pattern 10xx xxxx
    if ((b & 0xC0) != 0x80) {
      return TSCFG_ERR_INVALID;
    }

    *accum = ((*accum) << 6) + (b & 0x3F);
  }

  // Out of Unicode range
  if (*accum > 0x10FFFF) {
    return TSCFG_ERR_INVALID;
  }

  return TSCFG_OK;
}

/*
 * Return encoded length, or 0 if invalid
 */
static inline uint8_t tscfg_encoded_len(tscfg_char_t c) {
  if (c <= 0x07F) {
    return 1;
  } else if (c <= 0x7FF) {
    return 2;
  } else if (c <= 0xFFFF) {
    return 3;
  } else if (c <= 0x10FFFF) {
    return 4;
  } else {
    // Out of range
    return 0;
  }
}

/*
 * Assume that buffer is large enough
 * (either >= 4 or >= tscfg_encoded_len result)
 */
static inline void tscfg_encode(tscfg_char_t c, unsigned char *buf) {
  if (c <= 0x07F) {
    buf[0] = (unsigned char)c;
  } else if (c <= 0x7FF) {
    buf[1] = (unsigned char)(0x80 | (0x3F & c));
    c >>= 6;

    buf[0] = (unsigned char)(0xC0 | (0x1F & c));
  } else if (c <= 0xFFFF) {
    for (int i = 2; i >= 1; i--) {
      buf[i] = (unsigned char)(0x80 | (0x3F & c));
      c >>= 6;
    }
    buf[0] = (unsigned char)(0xE0 | (0x0F & c));
  } else if (c <= 0x10FFFF) {
    for (int i = 3; i >= 1; i--) {
      buf[i] = (unsigned char)(0x80 | (0x3F & c));
      c >>= 6;
    }
    buf[0] = (unsigned char)(0xF0 | (0x07 & c));
  } else {
    // Out of range, silently fail...
  }
}

#endif // __TSCONFIG_UTF8_H
