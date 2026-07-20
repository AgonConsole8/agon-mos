#ifndef MOS_FILE_H
#define MOS_FILE_H

#include "defines.h"
#include "ff.h"

extern TCHAR	cwd[256];

#define RESOLVE_OMIT_EXPAND			0x40
#define RESOLVE_MATCH_ALL_ATTRIBS	0x80

uint8_t isDirectory(char *path);
char * getFilepathPrefixEnd(char * filepath);
char * getFilepathLeafname(char * filepath);
int getDirectoryForPath(char * srcPath, char * dir, int * length, BYTE index);
int resolvePath(char * filepath, char * resolvedPath, int * length, BYTE * index, DIR * dir, BYTE flags);
int resolveRelativePath(char * path, char * resolved, int * length);
bool isMoslet(char * filepath);
int getResolvedPath(char * source, char ** resolvedPath, BYTE flags);
int copyFile(char * source, char * dest);

#endif /* MOS_FILE_H */
