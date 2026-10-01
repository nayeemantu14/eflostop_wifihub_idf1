/*
@file json.h
@brief handles very basic JSON with a minimal footprint on the system

This code is a lightly modified version of cJSON 1.4.7. cJSON is licensed under the MIT license:
Copyright (c) 2009 Dave Gamble

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

@see https://github.com/DaveGamble/cJSON
*/

#ifndef JSON_H_INCLUDED
#define JSON_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LOCAL PATCH (2.1.4 C2e): the longest JSON string json_print_ssid() writes for a 32-byte
 * SSID, quotes included and the terminator not: every byte as \u00XX.
 */
#define JSON_SSID_STR_MAX					(2 + 6 * 32)

/**
 * @brief LOCAL PATCH (2.1.4 C2e): writes an SSID as a JSON string, quotes included, never past
 * out_size. It replaces cJSON's json_print_string(), which wrote into a buffer of unchecked size.
 *
 * The SSID is the bytes at ssid up to the first NUL, ssid_size of them at most: a 32-byte SSID
 * field (wifi_config_t) has no terminator.
 *  - '"' and '\' are escaped. A control character (0x00-0x1F, 0x7F) is written as '?'.
 *  - An SSID that is valid UTF-8 is otherwise copied as it is, and *raw is cleared.
 *  - An SSID that is not (a Latin-1 or GBK one, say) is "raw": each of its bytes 0x80-0xFF is
 *    written as \u00XX, so every code point of the JSON string is one byte of the SSID, and *raw
 *    is set. JSON readers then see valid JSON, and the page can send the SSID's bytes back.
 *
 * @param raw set to whether the SSID was written raw; may be NULL.
 * @return the length written, the terminator excluded; 0 if the string did not fit in out_size,
 * with out set to "" (when out_size allows it).
 */
size_t json_print_ssid(const unsigned char *ssid, size_t ssid_size, char *out, size_t out_size, bool *raw);

#ifdef __cplusplus
}
#endif

#endif /* JSON_H_INCLUDED */
