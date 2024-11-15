// MOS File utility functions
//

#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "mos_file.h"
#include "mos_sysvars.h"

// Utility function to scan a filepath to find end of a prefix (colon)
char * getFilepathPrefixEnd(char * filepath) {
	if (strchr(filepath, ':') != NULL) {
		return strchr(filepath, ':');
	}
	return NULL;
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

// checkFileExists checks whether a file exists at the given path
int checkFileExists(char * path, char * leafname, FILINFO * fileinfo) {
	DIR dir;
	int result = f_findfirst(&dir, fileinfo, path, leafname);
	f_closedir(&dir);
	// TODO emulator is reporting FR_OK when leafname is not found
	// check on real hardware whether this is needed there
	if (fileinfo->fname[0] == '\0') {
		result = FR_NO_FILE;
	}
	return result;
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
					// Not enough space to store the full path - maybe return a memory error?
					result = FR_INT_ERR;
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
					// Not enough space to store the full path - maybe return a memory error?
					result = FR_INT_ERR;
				}
				*length = pathLength;
				break;
			}
		} else {
			// Construct a full path to compare against "after"
			char * fullpath = umm_malloc(pathLength);
			if (fullpath == NULL) {
				result = FR_INT_ERR;
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
			return FR_INT_ERR;
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
				return FR_INT_ERR;
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
				result = FR_INT_ERR;
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

			if (prefixResult == FR_NO_FILE && directoryExists == NULL) {
				// Cache first matching directory
				directoryExists = prefixPath;
			}

			umm_free(testPath);
			if (prefixResult == FR_OK) {
				// we found a definite match
				*length = resolvedLength;
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
		char leafChar = *(leafname - 1);
		*(leafname - 1) = '\0';
		// if we've set our path to be empty then our full path must have started with a slash
		result = matchRawPath(*path == '\0' ? "/" : path, leafname, resolvedPath, length, resolvedPath);		
		*(leafname - 1) = leafChar;
	} else {
		result = matchRawPath(".", leafname, resolvedPath, length, resolvedPath);
	}

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
			// return FR_INVALID_NAME;
			return FR_NOT_ENOUGH_CORE;
			// return FR_INT_ERR;
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
			// return FR_INVALID_NAME;
			return FR_NOT_ENOUGH_CORE;
			// return FR_INT_ERR;
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
			return FR_INT_ERR;
		}
		result = resolvePath(source, *resolvedPath, &length);
	}
	return result;
}
