// MOS File utility functions
//

#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "mos_file.h"
#include "mos_sysvars.h"

// Check if a path is a directory - path must be resolved
BOOL isDirectory(char *path) {
	FILINFO fil;
	FRESULT fr;

	if (path[0] == '\0' || strcmp(path, ".") == 0 || strcmp(path, "..") == 0 || strcmp(path, "/") == 0 || strcmp(path, "./") == 0 || strcmp(path, "../") == 0) {
		return TRUE;
	}

	// check if destination is a directory
	fr = f_stat(path, &fil);

	return (fr == FR_OK) && fil.fname[0] && (fil.fattrib & AM_DIR);
}

// Utility function to scan a filepath to find end of a prefix (colon)
char * getFilepathPrefixEnd(char * filepath) {
	return strchr(filepath, ':');
}

// Utility function to scan a filepath to find the leafname (last part)
char * getFilepathLeafname(char * filepath) {
	// scan backwards from the end of the string to find the last colon or slash, or beginning of string
	char * leafname = filepath + strlen(filepath);
	while (leafname > filepath) {
		if (leafname[-1] == ':' || leafname[-1] == '/') {
			break;
		}
		leafname--;
	}
	// check for special cases where we should return an empty string
	if ((leafname[0] == '.') && (leafname[1] == '\0' || (leafname[1] == '.' && leafname[2] == '\0'))) {
		return filepath + strlen(filepath);
	}
	return leafname;
}

// Resolve directory path (including prefix resolution) for a given srcPath
// Index specifies which prefix to use - ignored when path doesn't use a prefix
// NB this is purely string manipulation, no filesystem access, so no checking to see if path exists
//
int getDirectoryForPath(char * srcPath, char * dir, int * length, BYTE searchIndex) {
	char * path = getFilepathPrefixEnd(srcPath);
	char * leafname = getFilepathLeafname(srcPath);
	int dirLength = leafname - (path ? (path + 1) : srcPath);

	if (path != NULL) {
		// we have a prefix, so resolve it
		char * prefix;
		char * prefixPtr;	// Pointer to iterate over prefix
		char * prefixPath;	// Pointer to extract each path from prefix
		char * prefixToken = umm_malloc(path - srcPath + 6);
		int prefixIndex = 0;
		int prefixPathLength;
		int prefixResult = FR_OK;

		if (!prefixToken) {
			return MOS_OUT_OF_MEMORY;
		}
		sprintf(prefixToken, "%.*s$Path", path - srcPath, srcPath);

		prefix = expandVariableToken(prefixToken);
		umm_free(prefixToken);
		if (prefix == NULL) {
			if (dir != NULL && *length > 0) {
				*dir = '\0';
			}
			return FR_NO_PATH;
		}
		path++;		// Skip the colon

		prefixPtr = prefix;
		prefixResult = extractString(prefixPtr, &prefixPtr, ", ;", &prefixPath, EXTRACT_FLAG_AUTO_TERMINATE);
		while (prefixResult == FR_OK && prefixIndex < searchIndex) {
			prefixIndex++;
			prefixResult = extractString(prefixPtr, &prefixPtr, ", ;", &prefixPath, EXTRACT_FLAG_AUTO_TERMINATE);
		}

		if (prefixResult != FR_OK) {
			// no prefix at this index, or prefix is broken
			umm_free(prefix);
			if (dir != NULL && *length > 0) {
				*dir = '\0';
			}
			return FR_NO_PATH;
		}

		prefixPathLength = strlen(prefixPath) + dirLength + 1;

		// we have our prefix, so copy it to our dir
		if (dir != NULL && prefixPathLength <= *length) {
			sprintf(dir, "%s%.*s", prefixPath, dirLength, path);
		}
		*length = prefixPathLength;
		umm_free(prefix);
	} else {
		// no prefix, so just copy the path
		if (searchIndex != 0) {
			// we don't have a prefix, so we can't have an index
			if (dir != NULL && *length > 0) {
				*dir = '\0';
			}
			return FR_NO_PATH;
		}
		if (dir != NULL && dirLength <= *length) {
			sprintf(dir, "%.*s", dirLength, srcPath);
		}
		*length = dirLength + 1;
	}

	return FR_OK;
}

int getLengthForResolvedPath(char * filepath, int * length, BYTE * index) {
	DIR		dir;
	FILINFO fileinfo;
	int		result = FR_NO_PATH;
	int		fileResult;
	int		basePathLength;
	int		prefixIndex = index == NULL ? 0 : *index;
	int		pathResult;
	char *	searchPath = NULL;
	char *	leafname = getFilepathLeafname(filepath);
	bool	hasLeafname = leafname[0] != '\0';
	BYTE	successIndex = 255;

	pathResult = getDirectoryForPath(filepath, NULL, &basePathLength, prefixIndex);
	while (pathResult == FR_OK) {
		searchPath = umm_malloc(basePathLength);
		if (searchPath == NULL) {
			result = MOS_OUT_OF_MEMORY;
			break;
		}
		pathResult = getDirectoryForPath(filepath, searchPath, &basePathLength, prefixIndex);
		if (pathResult != FR_OK) {
			break;
		}

		fileResult = f_findfirst(&dir, &fileinfo, searchPath, hasLeafname ? leafname : NULL);
		while (fileResult == FR_OK) {
			int loopPathLength = basePathLength;
			if (hasLeafname && fileinfo.fname[0] == '\0') {
				fileResult = FR_NO_FILE;
			}
			if (fileResult == FR_NO_FILE) {
				loopPathLength += strlen(leafname);
			} else {
				loopPathLength += strlen(fileinfo.fname);
			}
			if (result != FR_OK && fileResult == FR_OK) {
				// We have a positive match for our path, so replace what we may already have
				// NB this may result in a smaller length being reported
				*length = loopPathLength;
			} else if (loopPathLength > *length) {
				*length = loopPathLength;
			}
			if (result != FR_OK) {
				// "upgrade" result until it becomes OK
				successIndex = prefixIndex;
				result = fileResult;
			}
			fileResult = f_findnext(&dir, &fileinfo);
			if (fileinfo.fname[0] == '\0') {
				break;
			}
		}
		prefixIndex++;
		umm_free(searchPath);
		pathResult = getDirectoryForPath(filepath, NULL, &basePathLength, prefixIndex);
	}
	if (index != NULL) {
		*index = successIndex < 255 ? successIndex : prefixIndex;
	}

	return result;
}

// resolvePath resolves a path, replacing path prefix and leafname with actual values
// if resolvedPath is NULL, only the length of the resolved path is returned
// if a DIR object is passed in, it will be used to find the next match
// together with the index parameter (for prefix resolution)
// if resolvedPath is not NULL, it needs to be long enough to hold the resolved path
// length is set on exit to the length of the resolved path
// returns:
// FR_OK if the path was resolved,
// FR_NO_FILE if the file was not found,
// FR_NO_PATH if the path was not found
// Or an error code if an error occurred
//
int resolvePath(char * filepath, char * resolvedPath, int * length, BYTE * index, DIR * dir) {
	int result = FR_OK;
	DIR * localDir = NULL;
	FILINFO fileinfo;
	BYTE prefixIndex = index ? *index : 0;
	bool newSearch = prefixIndex == 0;
	bool lengthCheck = resolvedPath == NULL;
	char * leafname = getFilepathLeafname(filepath);

	if (lengthCheck) {
		return getLengthForResolvedPath(filepath, length, NULL);
	}

	if (dir == NULL) {
		localDir = umm_malloc(sizeof(DIR));
		if (localDir == NULL) {
			return MOS_OUT_OF_MEMORY;
		}
		dir = localDir;
		newSearch = true;
	}

	if (resolvedPath != NULL && *length > 0) {
		*resolvedPath = '\0';
	}

	if (!newSearch) {
		result = f_findnext(dir, &fileinfo);
		if (result != FR_OK) {
			// something is broken
			umm_free(localDir);
			return result;
		}
		if (fileinfo.fname[0] == '\0') {
			// we need to move on to the next directory
			newSearch = true;
			result = FR_NO_PATH;	// default our result to no path
			f_closedir(dir);
		} else {
			// Match found, so fill in resolved path
			int pathLength = 0;
			// our prefixIndex should be the prefix *after* the one we are using, or zero if none set
			result = getDirectoryForPath(filepath, NULL, &pathLength, prefixIndex > 0 ? prefixIndex - 1 : 0);
			if (result != FR_OK) {
				// something went wrong
				umm_free(localDir);
				return result;
			}
			pathLength += strlen(fileinfo.fname);

			if (pathLength <= *length) {
				result = getDirectoryForPath(filepath, resolvedPath, &pathLength, prefixIndex > 0 ? prefixIndex - 1 : 0);
				if (result != FR_OK) {
					// something went wrong
					umm_free(localDir);
					return result;
				}
				sprintf(resolvedPath, "%s%s", resolvedPath, fileinfo.fname);
				*length = pathLength;
			} else {
				// not enough space
				umm_free(localDir);
				return MOS_OUT_OF_MEMORY;
			}
		}
	}

	if (newSearch) {
		char * searchPath = NULL;
		int pathLength = 0;
		int basePathLength = 0;
		bool found = false;

		while (!found) {
			basePathLength = 0;
			result = getDirectoryForPath(filepath, NULL, &basePathLength, prefixIndex);
			if (result != FR_OK) break;
			umm_free(searchPath);
			searchPath = umm_malloc(basePathLength);
			if (searchPath == NULL) {
				result = MOS_OUT_OF_MEMORY;
				break;
			}
			result = getDirectoryForPath(filepath, searchPath, &basePathLength, prefixIndex);
			if (result != FR_OK) break;

			result = f_findfirst(dir, &fileinfo, searchPath, leafname[0] == '\0' ? NULL : leafname);
			prefixIndex++;

			if (result != FR_NO_PATH) {
				found = true;
				if (result == FR_OK && leafname[0] != '\0' && fileinfo.fname[0] == '\0') {
					// Searching for a file, but not found in this directory - maybe it's in a later one?
					int newLength = 0;
					BYTE testIndex = prefixIndex;
					int testResult = FR_NO_FILE;
					result = FR_NO_FILE;
					testResult = getLengthForResolvedPath(filepath, &newLength, &testIndex);
					if (testResult == FR_OK) {
						// skip to match, and loop back around to fill in result
						if (testIndex >= prefixIndex) {
							prefixIndex = testIndex;
							found = false;
							continue;
						}
					}
				}
				pathLength = basePathLength + (fileinfo.fname[0] == '\0' ? strlen(leafname) : strlen(fileinfo.fname));
				if (pathLength <= *length) {
					sprintf(resolvedPath, "%s%s", searchPath, fileinfo.fname[0] == '\0' ? leafname : fileinfo.fname);
				} else {
					result = MOS_OUT_OF_MEMORY;
				}
			}
		}
		umm_free(searchPath);
	}

	if (result == FR_NO_PATH && resolvedPath != NULL && *length > 0) {
		*resolvedPath = '\0';
	}

	if (index && result != FR_NO_PATH) {
		*index = prefixIndex;
	}

	umm_free(localDir);
	return result;
}

// resolveRelativePath resolves a relative path to an absolute path
//
int resolveRelativePath(char * path, char * resolved, int length) {
	int result;
	char * leafname;
	char leafChar;

	leafname = getFilepathLeafname(path);
	leafChar = *leafname;
	if (leafname == path) {
		// only have a leafname, so just return cwd + leafname
		if (length >= strlen(path) + strlen(cwd) + 1) {
			if (leafChar == '\0') {
				sprintf(resolved, "%s", cwd);
			} else {
				sprintf(resolved, "%s/%s", cwd, path);
			}
			return FR_OK;
		} else {
			return MOS_OUT_OF_MEMORY;
		}
	}
	*leafname = '\0';

	result = f_chdir(path);
	if (result == FR_OK) {
		result = f_getcwd(resolved, length);
	}
	// append our leafname to resolved
	*leafname = leafChar;
	if (result == FR_OK) {
		if (strlen(resolved) + strlen(leafname) + 1 > length) {
			return MOS_OUT_OF_MEMORY;
		}
		if (leafChar != '\0') {
			sprintf(resolved, "%s/%s", resolved, leafname);
		}
	}
	// restore cwd
	f_chdir(cwd);
	return result;
}

// Check to see if a file is a moslet
// Parameters:
// - filepath: The path to the file
// Returns:
// - true if the file is a moslet, otherwise false
// NB filepath is expected to be a full path without relative path components
//
bool isMoslet(char * filepath) {
	char * checkPath = NULL;
	char * mosletPath;
	char * mosletPathStr;
	char * leaf = NULL;
	char leafChar;
	bool result = false;

	mosletPathStr = expandVariableToken("Moslet$Path");
	if (mosletPathStr == NULL) {
		// Mostlet$Path variable has been removed, so default it to /mos/
		mosletPath = "/mos/";
	} else {
		mosletPath = mosletPathStr;
	}

	// Temporarily terminate filepath at leafname
	// to allow match for exact directory
	leaf = getFilepathLeafname(filepath);
	leafChar = *leaf;
	*leaf = '\0';

	while (!extractString(mosletPath, &mosletPath, ", ;", &checkPath, EXTRACT_FLAG_AUTO_TERMINATE)) {
		if (pmatch(checkPath, filepath, MATCH_BEGINS_WITH | MATCH_CASE_INSENSITIVE | MATCH_DISABLE_HASH | MATCH_DISABLE_STAR) == 0) {
			// We have a match
			result = true;
			break;
		}
	}
	*leaf = leafChar;

	umm_free(mosletPathStr);

	return result;
}

// Get the resolved path for a given path
// Calling function is responsible for freeing the resolvedPath
int getResolvedPath(char * source, char ** resolvedPath) {
	int length = 0;
	int result = resolvePath(source, NULL, &length, NULL, NULL);
	if (result == FR_OK || result == FR_NO_FILE) {
		length++;
		*resolvedPath = umm_malloc(length);
		if (*resolvedPath == NULL) {
			return MOS_OUT_OF_MEMORY;
		}
		result = resolvePath(source, *resolvedPath, &length, NULL, NULL);
	}
	return result;
}

int copyFile(char * source, char * dest) {
	FIL src, dst;
	FRESULT fr;
	UINT br, bw;
	BYTE * buffer;

	buffer = umm_malloc(1024);
	if (!buffer) {
		return MOS_OUT_OF_MEMORY;
	}

	fr = f_open(&src, source, FA_READ);
	if (fr == FR_OK) {
		fr = f_open(&dst, dest, FA_WRITE | FA_CREATE_NEW);
		if (fr == FR_OK) {
			while (1) {
				fr = f_read(&src, buffer, sizeof(buffer), &br);
				if (fr != FR_OK || br == 0) {
					break;
				}
				fr = f_write(&dst, buffer, br, &bw);
				if (fr != FR_OK || bw < br) {
					break;
				}
			}
			f_close(&dst);
		}
		f_close(&src);
	}

	umm_free(buffer);
	return fr;
}
