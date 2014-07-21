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
    *accum = 0x7F & b;
    *len = 1;
    return TSCFG_OK;
  } else if (b <= 0xBF) {
    return TSCFG_ERR_INVALID;
  } else if (b <= 0xDF) { // Binary 110x xxxx
    *accum = 0x1F & b;
    *len = 2;
    return TSCFG_OK;
  } else if (b <= 0xEF) { // Binary 1110 xxxx
    *accum = 0x0F & b;
    *len = 3;
    return TSCFG_OK;
  } else if (b <= 0xF7) { // Binary 1111 0xxx
    *accum = 0x07 & b;
    *len = 4;
    return TSCFG_OK;
  } else if (b <= 0xFB) { // Binary 1111 10xx
    *accum = 0x03 & b;
    *len = 5;
    return TSCFG_OK;
  } else if (b <= 0xFD) { // Binary 1111 110x
    *accum = 0x01 & b;
    *len = 6;
    return TSCFG_OK;
  } else {
    return TSCFG_ERR_INVALID;
  }
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

  return TSCFG_OK;
}

#endif // __TSCONFIG_UTF8_H
