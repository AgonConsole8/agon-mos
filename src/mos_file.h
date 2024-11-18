#ifndef MOS_FILE_H
#define MOS_FILE_H

#include "defines.h"
#include "ff.h"

extern TCHAR	cwd[256];

BOOL isDirectory(char *path);
char * getFilepathPrefixEnd(char * filepath);
char * getFilepathLeafname(char * filepath);
int getDirectoryForPath(char * srcPath, char * dir, int * length, BYTE index);
int resolvePath(char * filepath, char * resolvedPath, int * length, BYTE * index, DIR * dir);
int resolveRelativePath(char * path, char * resolved, int length);
bool isMoslet(char * filepath);
int getResolvedPath(char * source, char ** resolvedPath);
int copyFile(char * source, char * dest);

#endif MOS_FILE_H
