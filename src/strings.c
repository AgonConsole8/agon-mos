/*
 * Title:			AGON MOS - Additional string functions
 * Author:			Leigh Brown, HeathenUK, and others
 * Created:			24/05/2023
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "umm_malloc.h"

int strcasecmp(const char *s1, const char *s2) {
	const unsigned char *p1 = (const unsigned char *)s1;
	const unsigned char *p2 = (const unsigned char *)s2;
	int result;

	if (p1 == p2) {
		return 0;
	}

	while ((result = tolower(*p1) - tolower(*p2++)) == 0) {
		if (*p1++ == '\0') {
			break;
		}
	}
	return result;
}

// Alternative to missing strnlen() in ZDS libraries
size_t mos_strnlen(const char *s, size_t maxlen) {
	size_t len = 0;
	while (len < maxlen && s[len] != '\0') {
		len++;
	}
	return len;
}

// Alternative to missing strdup() in ZDS libraries
char * mos_strdup(const char *s) {
	char *d = umm_malloc(strlen(s) + 1);  // Allocate memory
	if (d != NULL) {
		strcpy(d, s);      // Copy the string
	}
	return d;
}

// Alternative to missing strndup() in ZDS libraries
char * mos_strndup(const char *s, size_t n) {
	size_t len = mos_strnlen(s, n);
	char *d = umm_malloc(len + 1);  // Allocate memory for length plus null terminator

	if (d != NULL) {
		strncpy(d, s, len);  // Copy up to len characters
		d[len] = '\0';       // Null-terminate the string
	}

	return d;
}
