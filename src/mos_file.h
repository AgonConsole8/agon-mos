#ifndef MOS_FILE_H
#define MOS_FILE_H

#include "defines.h"
#include "ff.h"

extern TCHAR	cwd[256];

char * getFilepathPrefixEnd(char * filepath);
char * getFilepathLeafname(char * filepath);
int checkFileExists(char * path, char * leafname, FILINFO * fileinfo);
int matchRawPath(char * srcPath, char * srcPattern, char * destPath, int * length, char * after);
int resolvePath(char * filepath, char * resolvedPath, int * length);
int resolveRelativePath(char * path, char * resolved, int length);
bool isMoslet(char * filepath);
int getResolvedPath(char * source, char ** resolvedPath);

#endif MOS_FILE_H
