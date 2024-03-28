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

extern int strcasecmp(const char *s1, const char *s2);

size_t mos_strnlen(const char *s, size_t maxlen);

//Alternative to missing strdup() in ZDS libraries
char *mos_strdup(const char *s);

//Alternative to missing strndup() in ZDS libraries
char *mos_strndup(const char *s, size_t n);

#endif // STRINGS_H
