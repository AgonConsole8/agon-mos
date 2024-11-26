/*
 * Title:			AGON MOS - Additional string functions
 * Author:			Leigh Brown, HeathenUK, and others
 * Created:			24/05/2023
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "defines.h"
#include "strings.h"
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

char * stristr(const char * str, const char * substr) {
	int i;
	int c = tolower((unsigned char) * substr);
	if (c == '\0') {
		return (char *)str;
	}
	for (; *str; str++) {
		if (tolower((unsigned char)*str) == c) {
			for (i = 0;;) {
				if (substr[++i] == '\0') {
					return (char *) str;
				}
				if (tolower((unsigned char)str[i]) != tolower((unsigned char)substr[i])) {
					break;
				}
			}
		}
	}
	return NULL;
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

// Proper strcspn() implementation
size_t mos_strcspn(const char *s, const char *reject) {
	const char *p = s;
	while (*p != '\0') {
		if (strchr(reject, *p) != NULL) {
			break;
		}
		p++;
	}
	return p - s;
}

// Proper strspn() implementation
size_t mos_strspn(const char *s, const char *accept) {
	const char *p = s;
	while (*p != '\0') {
		if (strchr(accept, *p) == NULL) {
			break;
		}
		p++;
	}
	return p - s;
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

int pmatch(const char *pattern, const char *string, uint8_t flags) {
	bool caseInsensitive = flags & MATCH_CASE_INSENSITIVE;
	bool disableStar = flags & MATCH_DISABLE_STAR;
	bool disableHash = flags & MATCH_DISABLE_HASH;
	bool dotAsStar = flags & MATCH_DOT_AS_STAR;
	bool beginsWith = flags & MATCH_BEGINS_WITH;
	bool upToSpace = flags & MATCH_UP_TO_SPACE;

	if (*pattern == '\0' || (upToSpace && *pattern == ' ') || (beginsWith && dotAsStar && *pattern == '.')) {
		// if the pattern has been exhausted
		// return success if the string has also been exhausted, or we are doing a beginsWith test
		return (beginsWith || *string == '\0') ? 0 : -1;
	} else if (*pattern == '.' && dotAsStar && *(pattern + 1) == '\0') {
		// Dot as star wildcard (supported at end of string only) means one-or-more matching characters
		return *string == '\0' ? -1 : 0;
	} else if (*pattern == '*' && !disableStar) {
		// Skip the globbed wildcard and try to match the rest of the pattern with the current string
		// or skip one character in the string and try to match again
		if (pmatch(pattern + 1, string, flags) == 0 || (*string && pmatch(pattern, string + 1, flags) == 0)) {
			return 0;
		}
		return -1;
	} else {
		// Handle the '#' wildcard or exact character match
		char patternChar = caseInsensitive ? tolower(*pattern) : *pattern;
		char stringChar = caseInsensitive ? tolower(*string) : *string;

		if ((*pattern == '#' && !disableHash) || patternChar == stringChar) {
			return pmatch(pattern + 1, string + 1, flags);
		}

		// If the current characters don't match and it's not a wildcard, return the difference
		return stringChar - patternChar;
	}
}
