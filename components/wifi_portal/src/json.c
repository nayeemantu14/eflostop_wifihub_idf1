/*
@file json.c
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "json.h"


/* LOCAL PATCH (2.1.4 C2e): cJSON's escaper, json_print_string(), wrote into a buffer of unchecked
 * size: a control character costs 6 bytes, so a beaconed SSID of control characters overran the
 * network list's buffer (N4), and a 32-byte SSID field with no terminator ran on into the
 * password. json_print_ssid() replaces it, bounded by its output buffer. */

/**
 * @brief true if the n bytes at s are well-formed UTF-8 (RFC 3629: no overlong form, no
 * surrogate, nothing above U+10FFFF).
 */
static bool json_utf8_valid(const unsigned char *s, size_t n)
{
	size_t i = 0;
	while (i < n)
	{
		unsigned char c = s[i];
		size_t len;
		unsigned long cp;
		if (c < 0x80)
		{
			i++;
			continue;
		}
		else if (c >= 0xC2 && c <= 0xDF)
		{
			len = 2;
			cp = c & 0x1F;
		}
		else if (c >= 0xE0 && c <= 0xEF)
		{
			len = 3;
			cp = c & 0x0F;
		}
		else if (c >= 0xF0 && c <= 0xF4)
		{
			len = 4;
			cp = c & 0x07;
		}
		else
		{
			return false;
		}
		if (n - i < len)
		{
			return false;
		}
		for (size_t k = 1; k < len; k++)
		{
			if ((s[i + k] & 0xC0) != 0x80)
			{
				return false;
			}
			cp = (cp << 6) | (s[i + k] & 0x3F);
		}
		if ((len == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) ||
			(len == 4 && (cp < 0x10000 || cp > 0x10FFFF)))
		{
			return false;
		}
		i += len;
	}
	return true;
}

size_t json_print_ssid(const unsigned char *ssid, size_t ssid_size, char *out, size_t out_size, bool *raw)
{
	static const char hex[] = "0123456789abcdef";
	size_t n = (ssid != NULL) ? strnlen((const char *)ssid, ssid_size) : 0;
	bool is_raw = !json_utf8_valid(ssid, n);
	size_t o = 0;

	if (raw != NULL)
	{
		*raw = false;
	}
	/* the two quotes and the terminator at least */
	if (out == NULL || out_size < 3)
	{
		if (out != NULL && out_size > 0)
		{
			out[0] = '\0';
		}
		return 0;
	}

	out[o++] = '\"';
	for (size_t i = 0; i < n; i++)
	{
		unsigned char c = ssid[i];
		char esc[6];
		size_t len;
		if (c < 0x20 || c == 0x7F)
		{
			/* a control character: replaced */
			esc[0] = '?';
			len = 1;
		}
		else if (c == '\"' || c == '\\')
		{
			esc[0] = '\\';
			esc[1] = (char)c;
			len = 2;
		}
		else if (c >= 0x80 && is_raw)
		{
			/* a raw SSID's byte, as the code point of the same value */
			esc[0] = '\\';
			esc[1] = 'u';
			esc[2] = '0';
			esc[3] = '0';
			esc[4] = hex[c >> 4];
			esc[5] = hex[c & 0x0F];
			len = 6;
		}
		else
		{
			esc[0] = (char)c;
			len = 1;
		}
		/* room for it, the closing quote and the terminator */
		if (o + len + 2 > out_size)
		{
			out[0] = '\0';
			return 0;
		}
		memcpy(out + o, esc, len);
		o += len;
	}
	out[o++] = '\"';
	out[o] = '\0';

	if (raw != NULL)
	{
		*raw = is_raw;
	}
	return o;
}
