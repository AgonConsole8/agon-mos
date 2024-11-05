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
	char * leafname = filepath;
	while (extractString(&filepath, &leafname, ":/", EXTRACT_FLAG_NO_TERMINATOR)) {}
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
int resolvePath(char * filepath, char ** resolvedPath, int * length) {
	char * path = NULL;
	char * leafname = NULL;
	char * resolved = NULL;
	char leafChar = '\0';
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

		if (prefixToken == NULL) {
			return FR_INT_ERR;
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
		while (extractString(&prefixPtr, &prefixPath, ", ;", 0)) {
			char * testPath = umm_malloc((prefixPtr - prefixPath) + (path ? leafname - path : 0) + 1);
			if (testPath == NULL) {
				result = FR_INT_ERR;
				break;
			}
			sprintf(testPath, "%s%.*s", prefixPath, path ? leafname - path : 0, path);

			result = checkFileExists(testPath, leafname, &fileinfo);

			if (result == FR_OK) {
				// We have found a matching file
				int resolvedLength = strlen(testPath) + strlen(fileinfo.fname) + 1;
				if (resolvedPath != NULL && *length >= resolvedLength) {
					// resolve using filename from found file to replace wildcards
					sprintf(*resolvedPath, "%s%s", testPath, fileinfo.fname);
				}
				umm_free(testPath);
				*length = resolvedLength;
				break;
			} else if (result == FR_NO_FILE && directoryExists == NULL) {
				// Cache first matching directory
				directoryExists = prefixPath;
			}

			umm_free(testPath);
		}

		if (result != FR_OK) {
			if (path == NULL) {
				path = leafname;
			}
			if (directoryExists != NULL) {
				// File not found, but a directory was, so return path using that dir
				int resolvedLength = strlen(directoryExists) + 1 + strlen(path);
				if (resolvedPath != NULL && *length >= resolvedLength) {
					sprintf(*resolvedPath, "%s%s", directoryExists, path);
				}
				*length = resolvedLength;
				result = FR_NO_FILE;
			}
		}

		umm_free(prefix);
		// Result should reflect whether file was found (FR_OK), or directory was found (FR_NO_FILE), or no match was found (FR_NO_PATH)
		// for FR_OK and FR_NO_FILE, resolvedPath should be set to the full path to the file
		return result;
	}

	// No prefix
	// so check for the file at the given path
	path = filepath;

	if (path != leafname) {
		leafChar = *(leafname - 1);
		*(leafname - 1) = '\0';
		result = checkFileExists(path, leafname, &fileinfo);
	} else {
		result = checkFileExists(".", leafname, &fileinfo);
	}

	if (result == FR_OK || result == FR_NO_FILE) {
		char * fname = result == FR_OK ? fileinfo.fname : leafname;
		bool hasPath = path != leafname;
		int len = strlen(fname) + 1;
		if (hasPath) {
			len += leafname - path;
		}
		if (*length >= len) {
			if (hasPath) {
				sprintf(*resolvedPath, "%s/%s", path, fname);
			} else {
				sprintf(*resolvedPath, "%s", fname);
			}
		}
		*length = len;
	}
	if (leafChar != '\0') {
		*(leafname - 1) = leafChar;
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
	if (leafname == path) {
		// only have a leafname, so just return cwd + leafname
		if (length >= strlen(path) + strlen(cwd) + 1) {
			sprintf(resolved, "%s/%s", cwd, path);
			return FR_OK;
		} else {
			// return FR_INVALID_NAME;
			return FR_NOT_ENOUGH_CORE;
			// return FR_INT_ERR;
		}
	}
	leafChar = *leafname;
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
		sprintf(resolved, "%s/%s", resolved, leafname);
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
		result = resolvePath(source, resolvedPath, &length);
	}
	return result;
}
