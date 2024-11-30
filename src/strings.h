/*
 * Title:			AGON MOS - Additional string functions
 * Author:			Leigh Brown
 * Created:			24/05/2023
 * Last Updated:	24/05/2023
 *
 * Modinfo:
 */

#ifndef STRINGS_H
#define STRINGS_H

#include <ctype.h>

#define MATCH_CASE_INSENSITIVE		0x01
#define MATCH_DISABLE_STAR			0x02
#define MATCH_DISABLE_HASH			0x04
#define MATCH_DOT_AS_STAR			0x08
#define MATCH_BEGINS_WITH			0x10
#define MATCH_UP_TO_SPACE			0x20
#define MATCH_COMMANDS				MATCH_CASE_INSENSITIVE | MATCH_DOT_AS_STAR | MATCH_DISABLE_HASH | MATCH_DISABLE_STAR
#define MATCH_COMMANDS_AUTO			MATCH_CASE_INSENSITIVE | MATCH_DOT_AS_STAR | MATCH_DISABLE_HASH | MATCH_DISABLE_STAR

int strcasecmp(const char *s1, const char *s2);

char * stristr(const char * str, const char * substr);

size_t mos_strnlen(const char *s, size_t maxlen);

//Alternative to missing strdup() in ZDS libraries
char * mos_strdup(const char *s);

//Alternative to missing strndup() in ZDS libraries
char * mos_strndup(const char *s, size_t n);

size_t mos_strcspn(const char *s, const char *reject);
size_t mos_strspn(const char *s, const char *accept);

int pmatch(const char *pattern, const char *string, uint8_t flags);

#endif // STRINGS_H
