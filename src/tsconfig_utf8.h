/*
 * UTF-8 Functions
 *
 * Author: Tim Armstrong
 * tim.g.armstrong@gmail.com
 * Created: 20 July 2014
 * All rights reserved.
 */

#ifndef __TSCONFIG_UTF8_H
#define __TSCONFIG_UTF8_H

#include <stdint.h>

#include "tsconfig_common.h"

/*
 * Decoded unicode character.
 */
typedef uint32_t tscfg_char_t;

/*
 * Decode first byte of UTF-8 character.
 * This will detect any overlong encodings TODO: confirm
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
  } else if (b <= 0xFB) { // Binary 1111 10xx
    *len = 5;
    *accum = 0x03 & b;
  } else if (b <= 0xFD) { // Binary 1111 110x
    *len = 6;
    *accum = 0x01 & b;
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

#endif // __TSCONFIG_UTF8_H
