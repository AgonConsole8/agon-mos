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

	if (strcmp(path, ".") == 0 || strcmp(path, "..") == 0 || strcmp(path, "/") == 0) {
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
		bool found = false;

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

		found = extractString(&prefixPtr, &prefixPath, ", ;", 0);
		while (found && prefixIndex < searchIndex) {
			prefixIndex++;
			found = extractString(&prefixPtr, &prefixPath, ", ;", 0);
		}

		if (!found) {
			// no prefix at this index
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
		*length = dirLength;
	}

	return FR_OK;
}

// matchRawPath matches a source path pattern
// returning back a filled in destPath if it matches
int matchRawPath(char * srcPath, char * srcPattern, char * destPath, int * length, char * after) {
	FILINFO fileinfo;
	DIR dir;
	int result = FR_NO_FILE;
	int findResult;
	bool hasAfter = after != NULL && after[0] != '\0';
	// if we don't have an "after" we should find our first match, so set found to true
	bool found = !hasAfter;
	bool hasPattern;
	bool insertSlash = (strlen(srcPath) > 0 && srcPath[strlen(srcPath) - 1] != '/');
	int lengthAdjust = insertSlash ? 2 : 1;

	if (destPath == NULL) {
		// we are working out the maximum length
		*length = 0;
	}
	// Work around emulator not liking empty patterns
	if (srcPattern != NULL && srcPattern[0] == '\0') {
		srcPattern = NULL;
	}
	hasPattern = srcPattern != NULL;

	findResult = f_findfirst(&dir, &fileinfo, srcPath, srcPattern);
	while (findResult == FR_OK) {
		int pathLength = strlen(srcPath) + strlen(fileinfo.fname) + lengthAdjust;
		if (fileinfo.fname[0] == '\0') {
			// Reached end of matches (no more files)
			if (hasPattern) {
				pathLength += strlen(srcPattern);
			}
			if (!destPath) {
				// we are just working out the maximum length
				if (pathLength > *length) {
					*length = pathLength;
				}
				break;
			}
			// if we were searching for an "after" and we have not found, then we need to return empty path
			if (hasAfter) {
				if (found) {
					// we've already set our destPath to be empty, so just break
					break;
				}
				// TODO should we return FR_NO_PATH here?
				// we have an "after", but we haven't found it
				// right now this will be returning "no file", but will be leaving the destPath intact
			} else {
				if (*length >= pathLength) {
					// compose full path, including pattern if present, to allow for "save" to work
					sprintf(destPath, "%s%s%s", srcPath, insertSlash ? "/" : "", hasPattern ? srcPattern : "");
				} else {
					// Not enough space to store the full path
					result = MOS_OUT_OF_MEMORY;
				}
				*length = pathLength;
			}
			break;
		}

		if (found) {
			result = FR_OK;
			if (destPath == NULL) {
				// we are just working out the maximum length
				if (pathLength > *length) {
					*length = pathLength;
				}
			} else {
				if (*length >= pathLength) {
					sprintf(destPath, "%s%s%s", srcPath, insertSlash ? "/" : "", fileinfo.fname);
				} else {
					// Not enough space to store the full path
					result = MOS_OUT_OF_MEMORY;
				}
				*length = pathLength;
				break;
			}
		} else {
			// Construct a full path to compare against "after"
			char * fullpath = umm_malloc(pathLength);
			if (fullpath == NULL) {
				result = MOS_OUT_OF_MEMORY;
				break;
			}
			sprintf(fullpath, "%s%s%s", srcPath, insertSlash ? "/" : "", fileinfo.fname);
			found = (pmatch(after, fullpath, 0) == 0);
			if (found && destPath != NULL) {
				// clear our destPath to allow detection of when after was last item in a path
				destPath[0] = '\0';
			}
			umm_free(fullpath);
		}
		findResult = f_findnext(&dir, &fileinfo);
	}
	f_closedir(&dir);
	if (findResult == FR_NO_PATH) {
		result = FR_NO_PATH;
	}
	return result;
}

// resolvePath resolves a path, replacing path prefix and leafname with actual values
// if resolvedPath is NULL, only the length of the resolved path is returned
// if resolvedPath is not NULL, it needs to be long enough to hold the resolved path
// length is set on exit to the length of the resolved path
// returns:
// FR_OK if the path was resolved,
// FR_NO_FILE if the file was not found,
// FR_NO_PATH if the path was not found
// Or an error code if an error occurred
//
int resolvePath(char * filepath, char * resolvedPath, int * length) {
	char * path = NULL;
	char * leafname = NULL;
	FILINFO fileinfo;
	DIR dir;
	int result = FR_OK;

	// TODO add support for wildcard matching in path
	// if wildcard is in directory path we'd need to walk path
	// if it's just in filename we can use f_findfirst on the directory

	path = getFilepathPrefixEnd(filepath);
	leafname = getFilepathLeafname(path ? path : filepath);
	if (path != NULL) {
		// prefix is present, so find matching file
		char * prefix;		// Our prefix path variable
		char * prefixPtr;	// Pointer to iterate over prefix
		char * prefixPath;	// Pointer to start of current prefix path
		char * prefixToken = umm_malloc(path - filepath + 6);
		char * directoryExists = NULL;
		char * after = NULL;
		bool brokenOut = false;
		int resolvedLength = 0;

		if (prefixToken == NULL) {
			return MOS_OUT_OF_MEMORY;
		}

		// If we don't have a resolvePath, then reset the length
		if (resolvedPath == NULL) {
			*length = 0;
		}

		if (resolvedPath != NULL && *resolvedPath != '\0') {
			// We have a resolved path, so we need to find the next match
			after = mos_strdup(resolvedPath);
			if (after == NULL) {
				umm_free(prefixToken);
				return MOS_OUT_OF_MEMORY;
			}
		}

		sprintf(prefixToken, "%.*s$Path", path - filepath, filepath);
		prefix = expandVariableToken(prefixToken);
		umm_free(prefixToken);
		if (prefix == NULL) {
			return FR_NO_PATH;
		}
		path++;		// Skip the colon

		// our "path" from our filepath may not exist (might be a leafname)
		if (path == leafname) {
			path = NULL;
		}

		prefixPtr = prefix;
		// Iterate over path, checking whether we can find a matching file
		// TODO detection of exhausing all prefixes (via brokenOut) is kinda janky
		while (extractString(&prefixPtr, &prefixPath, ", ;", 0)) {
			int resolvedLength = *length;
			int prefixResult;
			char * testPath = umm_malloc((prefixPtr - prefixPath) + (path ? leafname - path : 0) + 2);
			if (testPath == NULL) {
				result = MOS_OUT_OF_MEMORY;
				brokenOut = true;
				break;
			}
			sprintf(testPath, "%s%.*s", prefixPath, path ? leafname - path : 0, path);

			prefixResult = matchRawPath(testPath, leafname, resolvedPath, &resolvedLength, after);
			if (after && prefixResult == FR_NO_FILE && (resolvedPath != NULL && (strlen(resolvedPath) == 0))) {
				// Reached the end of the directory which must have contained "after",
				// so we need to remove the "after" to continue searching and get next match
				umm_free(after);
				after = NULL;
			}

			if ((prefixResult == FR_OK || prefixResult == FR_NO_FILE) && *length < resolvedLength) {
				*length = resolvedLength;
			}

			if (prefixResult == FR_NO_FILE && directoryExists == NULL) {
				// Cache first matching directory
				directoryExists = prefixPath;
			}

			umm_free(testPath);
			if (prefixResult == FR_OK) {
				// we found a definite match
				result = FR_OK;
				if (resolvedPath != NULL) {
					brokenOut = true;
					break;
				}
			}
		}

		if (!brokenOut && resolvedPath != NULL) {
			// We didn't find a match in any of our paths, and weren't just working out the length
			result = FR_NO_PATH;
		}

		if (result != FR_OK) {
			if (path == NULL) {
				path = leafname;
			}
			if (directoryExists != NULL) {
				// File not found, but a directory was, so return path using that dir
				int resolvedLength = strlen(directoryExists) + 1 + strlen(path);
				if (resolvedPath != NULL && *length >= resolvedLength) {
					sprintf(resolvedPath, "%s%s", directoryExists, path);
				}
				*length = resolvedLength;
				result = FR_NO_FILE;
			}
		}

		umm_free(after);
		umm_free(prefix);
		// Result should reflect whether file was found (FR_OK), or directory was found (FR_NO_FILE), or no match was found (FR_NO_PATH)
		// for FR_OK and FR_NO_FILE, resolvedPath should be set to the full path to the file
		if (*length == 0) {
			result = FR_NO_PATH;
		}
		return result;
	}

	// No prefix
	// so check for the file at the given path
	path = filepath;

	if (path != leafname) {
		// extract path from filepath and do a match
		path = mos_strndup(filepath, leafname - filepath);
		if (path == NULL) {
			return MOS_OUT_OF_MEMORY;
		}
		result = matchRawPath(path, leafname, resolvedPath, length, resolvedPath);		
		umm_free(path);
	} else {
		// no path element means we are looking for a file in the current directory
		result = matchRawPath(".", leafname, resolvedPath, length, resolvedPath);
	}

	return result;
}


// newResolvePath resolves a path, replacing path prefix and leafname with actual values
// if a DIR object is passed in, it will be used to find the next match
// together with the index parameter (for prefix resolution)
int newResolvePath(char * filepath, char * resolvedPath, int * length, BYTE * index, DIR * dir) {
	int result = FR_OK;
	DIR * localDir = NULL;
	FILINFO fileinfo;
	BYTE prefixIndex = index ? *index : 0;
	bool newSearch = prefixIndex == 0;
	char * leafname = getFilepathLeafname(filepath);

	if (dir == NULL) {
		localDir = umm_malloc(sizeof(DIR));
		if (localDir == NULL) {
			return MOS_OUT_OF_MEMORY;
		}
		dir = localDir;
		newSearch = true;
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
			if (resolvedPath && *length > 0) {
				*resolvedPath = '\0';
			}
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

			if (!resolvedPath) {
				// we are just working out the length
				if (pathLength > *length) {
					*length = pathLength;
				}
			} else {
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
	}

	if (newSearch) {
		char * searchPath = NULL;
		int pathLength = 0;
		bool found = false;

		while (!found) {
			result = getDirectoryForPath(filepath, NULL, &pathLength, prefixIndex);
			if (result != FR_OK) break;
			umm_free(searchPath);
			searchPath = umm_malloc(pathLength);
			if (searchPath == NULL) {
				result = MOS_OUT_OF_MEMORY;
				break;
			}
			result = getDirectoryForPath(filepath, searchPath, &pathLength, prefixIndex);
			if (result != FR_OK) break;

			result = f_findfirst(dir, &fileinfo, searchPath, leafname[0] == '\0' ? NULL : leafname);
			prefixIndex++;

			if (result != FR_NO_PATH) {
				found = true;
				pathLength += strlen(fileinfo.fname);
				if (result == FR_OK && leafname[0] != '\0' && fileinfo.fname[0] == '\0') {
					// Searching for a file, but not found in this directory - maybe it's in a later one?
					int newLength = 0;
					BYTE testIndex = prefixIndex;
					int testResult = FR_NO_FILE;
					result = FR_NO_FILE;
					while (testResult != FR_OK && testResult != FR_NO_PATH) {
						testResult = newResolvePath(filepath, NULL, &newLength, &testIndex, NULL);
					}
					if (testResult == FR_OK) {
						// skip to match, and loop back around to fill in result
						prefixIndex = testIndex - 1;
						found = false;
						continue;
					}
				}
				if (resolvedPath) {
					if (pathLength <= *length) {
						sprintf(resolvedPath, "%s%s", searchPath, result == FR_NO_FILE ? leafname : fileinfo.fname);
					} else {
						result = MOS_OUT_OF_MEMORY;
					}
				} else {
					// we are just working out the length
					if (pathLength > *length) {
						*length = pathLength;
					}
				}
			}
		}
		umm_free(searchPath);
	}

	if (index) {
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

	while (extractString(&mosletPath, &checkPath, ", ;", 0)) {
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
	int result = resolvePath(source, NULL, &length);
	if (result == FR_OK || result == FR_NO_FILE) {
		length++;
		*resolvedPath = umm_malloc(length);
		if (*resolvedPath == NULL) {
			return MOS_OUT_OF_MEMORY;
		}
		**resolvedPath = '\0';
		result = resolvePath(source, *resolvedPath, &length);
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
