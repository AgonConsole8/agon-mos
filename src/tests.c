#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "umm_malloc.h"
#include "ff.h"
#include "mos.h"
#include "mos_file.h"
#include "mos_sysvars.h"

#include "tests.h"

#if DEBUG > 0

#define MG_MAX_ITEMS	64
#define MG_ITERS 1000
struct mg_item_t {
	int *ptr;
	int num;
};

bool showAllAsserts = false;

// fill the allocated region with its base pointer value
static void malloc_grind_fill(struct mg_item_t *item) {
	int i;
	for (i=0; i<item->num; i++) {
		item->ptr[i] = (int)item->ptr;
	}
}

static BOOL malloc_grind_validate(struct mg_item_t *item) {
	int i;
	for (i=0; i<item->num; i++) {
		if (item->ptr[i] != (int)item->ptr) {
			return 0;
		}
	}
	return 1;
}

void malloc_grind(bool verbose) {
	int iter, num, idx;
	BOOL status = 1;
	struct mg_item_t *items = umm_malloc(sizeof(struct mg_item_t) * MG_MAX_ITEMS);

	if (items == NULL) {
		printf("Insufficient RAM for test\r\n");
		return;
	}
	memset(items, 0, sizeof(struct mg_item_t) * MG_MAX_ITEMS);

	for (iter=0; iter<MG_ITERS; iter++) {
		idx = rand() % MG_MAX_ITEMS;

		if (items[idx].ptr == 0) {
			num = (rand() % 64) + 1;

			items[idx].ptr = umm_malloc(num * sizeof(struct mg_item_t));
			items[idx].num = num;
			if (items[idx].ptr) {
				malloc_grind_fill(&items[idx]);
				printf("+");
			} else {
				printf("x");
			}
		} else {
			if (!malloc_grind_validate(&items[idx])) {
				status = 0;
				goto cleanup;
			}
			umm_free(items[idx].ptr);
			items[idx].ptr = 0;
			printf("-");
		}
	}
cleanup:
	for (idx=0; idx < MG_MAX_ITEMS; idx++) {
		if (items[idx].ptr) {
			malloc_grind_validate(&items[idx]);
			umm_free(items[idx].ptr);
		}
	}
	umm_free(items);
	if (status) {
		printf("\r\nmalloc grind test passed!\r\n");
	} else {
		printf("\r\nmalloc grind test FAILED!\r\n");
	}
}

bool expectEq(char * check, int actual, int expected) {
	bool pass = actual == expected;
	if (!pass || showAllAsserts) {
		printf("%s: %s", check, pass ? "PASS" : "FAIL");
		if (!pass) {
			printf("\n\r    (expected %d == %d)", expected, actual);
		}
		printf("\n\r");
	}
	return pass;
}

bool expectNotEq(char * check, int actual, int expected) {
	bool pass = actual != expected;
	if (!pass || showAllAsserts) {
		printf("%s: %s", check, pass ? "PASS" : "FAIL");
		if (!pass) {
			printf("\n\r    (expected %d != %d)", expected, actual);
		}
		printf("\n\r");
	}
	return pass;
}

bool expectStrEq(char * check, char * actual, char * expected) {
	bool pass = strcmp(actual, expected) == 0;
	if (!pass || showAllAsserts) {
		printf("%s: %s", check, pass ? "PASS" : "FAIL");
		if (!pass) {
			printf("\n\r    (expected \"%s\" == \"%s\")", expected, actual);
		}
		printf("\n\r");
	}
	return pass;
}

void path_tests(bool verbose) {
	FRESULT fr;
	DIR dir;
	FILINFO fno;
	FIL file;
	char *tempString;
	char tempBuffer[256];
	char tempBuffer2[256];
	int length = 255;
	int i;
	BYTE index;
	bool passed = true;

	showAllAsserts = verbose;

	printf("Running path tests...\r\n");

	// create a test folder, and test files
	fr = f_mkdir("/path-tests-tmp");
	passed = expectEq("f_mkdir on new directory returns FR_OK", fr, FR_OK) && passed;
	// if directory already exists, emulator returns 1 (FR_INT_ERR) - should return 8
	if (fr != FR_OK) {
		printf("Path tests require a working SD card, a writable filesystem,\n\rand must not have a directory named '/path-tests-tmp'.\r\n");
		return;
	}

	// Create an empty sub-directory, and a few test files
	fr = f_mkdir("/path-tests-tmp/subdir");
	for (i=0; i<5; i++) {
		sprintf(tempBuffer, "/path-tests-tmp/testfile-%d.txt", i);
		fr = f_open(&file, tempBuffer, FA_CREATE_ALWAYS | FA_WRITE);
		f_close(&file);
	}

	// check exact behaviour of fatfs APIs on hardware vs emulator

	// what does opendir do when there's no directory
	passed = expectEq("f_opendir on non-existent directory returns FR_NO_PATH", f_opendir(&dir, "non-existent-directory"), FR_NO_PATH) && passed;
	f_closedir(&dir);
	passed = expectEq("f_opendir on non-existent sub-directory returns FR_NO_PATH", f_opendir(&dir, "/path-tests-tmp/non-existent-directory"), FR_NO_PATH) && passed;
	f_closedir(&dir);

	// what does findfirst do when there's no directory?
	passed = expectEq("f_findfirst on non-existent directory returns FR_NO_PATH", f_findfirst(&dir, &fno, "non-existent-directory", "*"), FR_NO_PATH) && passed;
	f_closedir(&dir);

	// what does findfirst do when there's no leafname/pattern (empty string)
	passed = expectEq("f_findfirst empty pattern on valid directory returns FR_OK", f_findfirst(&dir, &fno, "/path-tests-tmp/", ""), FR_OK) && passed;
	// NB The following test fails on the emulator, as it requires a "." or NULL rather than an empty pattern
	passed = expectStrEq("  the returned filename (empty pattern, valid dir) should be empty", fno.fname, "") && passed;
	f_closedir(&dir);

	passed = expectEq("find_first null pattern on valid directory returns FR_OK", f_findfirst(&dir, &fno, "/path-tests-tmp/", NULL), FR_OK) && passed;
	passed = expectStrEq("  the returned filename (null pattern, valid dir) should be empty", fno.fname, "") && passed;
	f_closedir(&dir);

	passed = expectEq("f_findfirst dot pattern on valid directory returns FR_OK", f_findfirst(&dir, &fno, "/path-tests-tmp/", "."), FR_OK) && passed;
	passed = expectStrEq("  the returned filename should be empty", fno.fname, "") && passed;
	f_closedir(&dir);

	passed = expectEq("f_findfirst empty pattern on valid (empty) directory returns FR_OK", f_findfirst(&dir, &fno, "/path-tests-tmp/subdir", ""), FR_OK) && passed;
	passed = expectStrEq("  the returned filename should be empty", fno.fname, "") && passed;
	f_closedir(&dir);

	passed = expectEq("f_findfirst valid pattern on valid directory returns FR_OK", f_findfirst(&dir, &fno, "/path-tests-tmp/", "*"), FR_OK) && passed;
	passed = expectNotEq("  the returned filename should not be empty", fno.fname[0], 0) && passed;
	f_closedir(&dir);

	// what does findfirst do when there's no matching pattern
	passed = expectEq("f_findfirst on valid directory non-existent file returns FR_OK", f_findfirst(&dir, &fno, "/path-tests-tmp/", "non-existent-file"), FR_OK) && passed;
	passed = expectStrEq("  the returned filename should be empty", fno.fname, "") && passed;
	f_closedir(&dir);

	// check what f_stat returns - first of all when there's no pattern
	passed = expectEq("f_stat on non-existent directory returns FR_NO_PATH", f_stat("/non-existent-directory/file", &fno), FR_NO_PATH) && passed;
	passed = expectStrEq("  the returned filename should be empty", fno.fname, "") && passed;
	f_closedir(&dir);

	passed = expectEq("f_stat on non-existent file returns FR_NO_FILE", f_stat("/path-tests-tmp/non-existent-file", &fno), FR_NO_FILE) && passed;
	passed = expectStrEq("  the returned filename should be empty", fno.fname, "") && passed;
	f_closedir(&dir);

	// what does f_stat do when there is a pattern?
	passed = expectEq("f_stat on non-existent directory with leaf pattern returns FR_NO_PATH", f_stat("/non-existent-directory/*", &fno), FR_NO_PATH) && passed;
	passed = expectStrEq("  the returned filename should be empty", fno.fname, "") && passed;
	f_closedir(&dir);

	passed = expectEq("f_stat on valid directory with a leaf pattern returns FR_INVALID_NAME", f_stat("/path-tests-tmp/*", &fno), FR_INVALID_NAME) && passed;
	passed = expectStrEq("  the returned filename should be empty", fno.fname, "") && passed;
	f_closedir(&dir);

	if (!passed || verbose) printf("\n\r");

	// matchRawPath will always resturn "no path" when matching a non-existent directory
	length = 0;
	passed = expectEq("matchRawPath on non-existent directory returns FR_NO_PATH", matchRawPath("/non-existent-directory", "*", NULL, &length, NULL), FR_NO_PATH) && passed;
	passed = expectEq("matchRawPath on non-existent directory returns FR_NO_PATH", matchRawPath("/non-existent-directory/", "*", NULL, &length, NULL), FR_NO_PATH) && passed;
	passed = expectEq("  length should still be 0", length, 0) && passed;

	passed = expectEq("matchRawPath on an existing directory (as pattern) returns FR_OK", matchRawPath("/", "path-tests-tmp", NULL, &length, NULL), FR_OK) && passed;
	passed = expectEq("  length should be 16", length, 16) && passed;

	passed = expectEq("matchRawPath on an existing directory returns FR_OK and path", matchRawPath("/", "path-tests-tmp", tempBuffer, &length, NULL), FR_OK) && passed;
	passed = expectStrEq("  result should be /path-tests-tmp", tempBuffer, "/path-tests-tmp") && passed;

	tempBuffer[0] = '\0';
	length = 255;
	// Emulator requires a "." or NULL rather than an empty pattern for findfirst, so matchRawPath must cope
	passed = expectEq("matchRawPath on a valid path with no pattern returns FR_NO_FILE", matchRawPath("/path-tests-tmp/", "", tempBuffer, &length, NULL), FR_NO_FILE) && passed;
	passed = expectStrEq("  result should match path", tempBuffer, "/path-tests-tmp/") && passed;

	// tempBuffer[0] = '\0';
	length = 255;
	passed = expectEq("matchRawPath on empty directory with wildcard pattern returns FR_NO_FILE", matchRawPath("/path-tests-tmp/subdir", "*", tempBuffer, &length, NULL), FR_NO_FILE) && passed;
	passed = expectStrEq("  result (empty with wildcard pattern) should match path", tempBuffer, "/path-tests-tmp/subdir/*") && passed;
	length = 255;
	passed = expectEq("matchRawPath on empty directory with pattern returns FR_NO_FILE", matchRawPath("/path-tests-tmp/subdir", "nofile", tempBuffer, &length, NULL), FR_NO_FILE) && passed;
	passed = expectStrEq("  result (empty with filename) match path", tempBuffer, "/path-tests-tmp/subdir/nofile") && passed;

	// length = 255;
	fr = matchRawPath("/path-tests-tmp", "testfile-1.txt", NULL, &length, NULL);
	passed = expectEq("matchRawPath on a valid path with pattern returns FR_OK", matchRawPath("/path-tests-tmp", "testfile-1.txt", tempBuffer, &length, NULL), FR_OK) && passed;
	passed = expectStrEq("  result should be /path-tests-tmp/testfile-1.txt", tempBuffer, "/path-tests-tmp/testfile-1.txt") && passed;
	passed = expectEq("  length should match string length + 1", length, strlen(tempBuffer) + 1) && passed;

	tempBuffer[0] = '\0';

	// iterate over a directory using a wildcard, passing in our result buffer to get next matches
	for (i=0; i<5; i++) {
		length = 255;
		fr = matchRawPath("/path-tests-tmp", "testfile-*.txt", tempBuffer, &length, tempBuffer);
		passed = expectEq("matchRawPath check returned pattern match", pmatch("/path-tests-tmp/testfile-*.txt", tempBuffer, MATCH_CASE_INSENSITIVE), 0) && passed;
	}
	passed = expectEq("matchRawPath with pattern returns FR_OK after all files matched", fr, FR_OK) && passed;

	// We've matched 5 times, so our test file should match our last file
	passed = expectStrEq("  last match should be /path-tests-tmp/testfile-4.txt", tempBuffer, "/path-tests-tmp/testfile-4.txt") && passed;

	// matchRawPath should return FR_NO_FILE when we've matched all files
	passed = expectEq("matchRawPath with pattern returns FR_NO_FILE after all files matched", matchRawPath("/path-tests-tmp", "testfile-*.txt", tempBuffer, &length, tempBuffer), FR_NO_FILE) && passed;
	passed = expectStrEq("  result should be empty", tempBuffer, "") && passed;

	length = 255;
	passed = expectEq("matchRawPath on a relative path with pattern returns FR_OK", matchRawPath("../../..", "path-tests-tmp", tempBuffer, &length, NULL), FR_OK) && passed;
	passed = expectStrEq("  result should be ../../../path-tests-tmp", tempBuffer, "../../../path-tests-tmp") && passed;

	length = 255;
	passed = expectEq("matchRawPath on a relative path with pattern returns FR_OK", matchRawPath("../../../path-tests-tmp", "testfile-1.txt", tempBuffer, &length, NULL), FR_OK) && passed;
	passed = expectStrEq("  result should be ../../../path-tests-tmp/testfile-1.txt", tempBuffer, "../../../path-tests-tmp/testfile-1.txt") && passed;

	length = 255;
	passed = expectEq("matchRawPath on a relative path with no pattern returns FR_NO_FILE", matchRawPath("../../..", NULL, tempBuffer, &length, NULL), FR_NO_FILE) && passed;
	passed = expectStrEq("  result (relative path, no pattern) should be ../../../", tempBuffer, "../../../") && passed;

	f_chdir("/path-tests-tmp/subdir");
	length = 255;
	passed = expectEq("matchRawPath on a non-existant filename in current directory returns FR_NO_FILE", matchRawPath(".", "test.BBC", tempBuffer, &length, NULL), FR_NO_FILE) && passed;
	passed = expectStrEq("  result (non-existant filename) should match path", tempBuffer, "./test.BBC") && passed;
	passed = expectEq("  length should match string length + 1", length, strlen(tempBuffer) + 1) && passed;
	f_chdir(cwd);

	if (!passed || verbose) printf("\n\r");

	// check resolveRelativePath - path to check needs to be writable (not in ROM)
	tempString = mos_strdup("../../../path-tests-tmp/testfile-1.txt");
	passed = expectEq("resolveRelativePath on a relative path with pattern returns FR_OK", resolveRelativePath(tempString, tempBuffer2, 255), FR_OK) && passed;
	passed = expectStrEq("  result (resolveRelativePath) should be /path-tests-tmp/testfile-1.txt", tempBuffer2, "/path-tests-tmp/testfile-1.txt") && passed;
	umm_free(tempString);

	f_chdir("/path-tests-tmp/subdir");
	tempString = mos_strdup("../../../../../..");
	passed = expectEq("resolveRelativePath on a relative path to root with pattern returns FR_OK", resolveRelativePath(tempString, tempBuffer2, 255), FR_OK) && passed;
	passed = expectStrEq("  result (resolveRelativePath) should be /", tempBuffer2, "/") && passed;
	umm_free(tempString);
	f_chdir(cwd);

	// getFilepathLeafname should return leafname of a path
	// unless path ends in a slash, in which case it should return an empty string
	// or ending in a dot or double dot, which should also return an empty string
	passed = expectStrEq("getFilepathLeafname on empty string returns empty string", getFilepathLeafname(""), "") && passed;
	passed = expectStrEq("getFilepathLeafname on / returns empty string", getFilepathLeafname("/"), "") && passed;
	passed = expectStrEq("getFilepathLeafname on . returns empty string", getFilepathLeafname("."), "") && passed;
	passed = expectStrEq("getFilepathLeafname on .. returns empty string", getFilepathLeafname(".."), "") && passed;
	passed = expectStrEq("getFilepathLeafname on /foo returns foo", getFilepathLeafname("/foo"), "foo") && passed;
	passed = expectStrEq("getFilepathLeafname on /foo/ returns empty string", getFilepathLeafname("/foo/"), "") && passed;
	passed = expectStrEq("getFilepathLeafname on /foo/bar returns bar", getFilepathLeafname("/foo/bar"), "bar") && passed;
	passed = expectStrEq("getFilepathLeafname on /foo/bar/ returns empty string", getFilepathLeafname("/foo/bar/"), "") && passed;
	passed = expectStrEq("getFilepathLeafname on /foo/bar/.. returns empty string", getFilepathLeafname("/foo/bar/.."), "") && passed;
	passed = expectStrEq("getFilepathLeafname on /foo/bar/. returns empty string", getFilepathLeafname("/foo/bar/."), "") && passed;
	passed = expectStrEq("getFilepathLeafname on foo/bar/.. returns empty string", getFilepathLeafname("foo/bar/.."), "") && passed;
	passed = expectStrEq("getFilepathLeafname on foo/bar/.Z returns .Z", getFilepathLeafname("foo/bar/.Z"), ".Z") && passed;

	if (!passed || verbose) printf("\n\r");

	// Tests for resolvePath
	// which should replace prefix with sysvar-expanded value

	// First we need to set up a path-tests$path sysvar
	// and then we can test resolvePath
	tempString = mos_strdup("/path-tests-tmp/");
	createOrUpdateSystemVariable("Path-Tests$Path", MOS_VAR_STRING, tempString);

	// use getResolvedPath to resolve our path into tempString
	fr = getResolvedPath("path-tests:file.txt", &tempString);
	passed = expectEq("getResolvedPath on path-tests:file.txt returns FR_NO_FILE", fr, FR_NO_FILE) && passed;
	passed = expectStrEq("  resolved path should be /path-tests-tmp/file.txt", tempString, "/path-tests-tmp/file.txt") && passed;
	umm_free(tempString);

	// make sure we have an empty target buffer
	// tempString = umm_malloc(256);
	*tempBuffer = '\0';
	length = 255;
	fr = resolvePath("path-tests:file.txt", tempBuffer, &length);
	passed = expectEq("resolvePath on path-tests:file.txt returns FR_NO_FILE", fr, FR_NO_FILE) && passed;
	passed = expectStrEq("  resolved path should be /path-tests-tmp/file.txt", tempBuffer, "/path-tests-tmp/file.txt") && passed;

	*tempBuffer = '\0';
	length = 255;
	fr = resolvePath("path-tests:unknown/file.txt", tempBuffer, &length);
	passed = expectEq("resolvePath on path-tests:unknown/file.txt returns FR_NO_PATH", fr, FR_NO_PATH) && passed;
	passed = expectStrEq("  resolved path should be empty", tempBuffer, "") && passed;

	*tempBuffer = '\0';
	length = 255;
	fr = resolvePath("path-tests:subdir", tempBuffer, &length);
	passed = expectEq("resolvePath on path-tests:subdir returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  resolved path should be /path-tests-tmp/subdir", tempBuffer, "/path-tests-tmp/subdir") && passed;

	*tempBuffer = '\0';
	length = 255;
	fr = resolvePath("path-tests:subdir/file.txt", tempBuffer, &length);
	passed = expectEq("resolvePath on path-tests:subdir/file.txt returns FR_NO_FILE", fr, FR_NO_FILE) && passed;
	passed = expectStrEq("  resolved path should be /path-tests-tmp/subdir/file.txt", tempBuffer, "/path-tests-tmp/subdir/file.txt") && passed;

	*tempBuffer = '\0';
	length = 255;
	fr = resolvePath("path-tests:*.txt", tempBuffer, &length);
	passed = expectEq("resolvePath on path-tests:*.txt returns FR_OK", fr, FR_OK) && passed;
	passed = expectEq("  resolved path matches pattern", pmatch("/path-tests-tmp/testfile-*.txt", tempBuffer, MATCH_CASE_INSENSITIVE), 0) && passed;

	*tempBuffer = '\0';
	length = 255;
	fr = resolvePath("..", tempBuffer, &length);
	passed = expectEq("resolvePath on .. returns FR_NO_FILE", fr, FR_NO_FILE) && passed;
	passed = expectStrEq("  resolved path should be ../", tempBuffer, "../") && passed;

	*tempBuffer = '\0';
	length = 255;
	fr = resolvePath("../../..", tempBuffer, &length);
	passed = expectEq("resolvePath on ../../.. returns FR_NO_FILE", fr, FR_NO_FILE) && passed;
	passed = expectStrEq("  resolved path should be ../../../", tempBuffer, "../../../") && passed;

	// add in subdir to our search path
	tempString = mos_strdup("/path-tests-tmp/subdir/, /path-tests-tmp/");
	createOrUpdateSystemVariable("Path-Tests$Path", MOS_VAR_STRING, tempString);

	*tempBuffer = '\0';
	length = 255;
	// first directory should be the preferred match
	fr = resolvePath("path-tests:file.txt", tempBuffer, &length);
	passed = expectEq("resolvePath on path-tests:file.txt returns FR_NO_FILE", fr, FR_NO_FILE) && passed;
	passed = expectStrEq("  resolved path should be /path-tests-tmp/subdir/file.txt", tempBuffer, "/path-tests-tmp/subdir/file.txt") && passed;

	// actual match of subdir, which is also a path
	*tempBuffer = '\0';
	length = 255;
	fr = resolvePath("path-tests:subdir", tempBuffer, &length);
	passed = expectEq("resolvePath on path-tests:subdir returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  resolved path should be /path-tests-tmp/subdir", tempBuffer, "/path-tests-tmp/subdir") && passed;

	// match to just path prefix should return first match - should be "no file" as no filename is given, just a path match
	*tempBuffer = '\0';
	length = 255;
	fr = resolvePath("path-tests:", tempBuffer, &length);
	passed = expectEq("resolvePath on path-tests: returns FR_NO_FILE", fr, FR_NO_FILE) && passed;
	passed = expectStrEq("  resolved path should be /path-tests-tmp/subdir/", tempBuffer, "/path-tests-tmp/subdir/") && passed;

	// non-existent prefix
	*tempBuffer = '\0';
	length = 255;
	fr = resolvePath("unknown-path-prefix:file.txt", tempBuffer, &length);
	passed = expectEq("resolvePath on unknown-path-prefix:file.txt returns FR_NO_PATH", fr, FR_NO_PATH) && passed;
	passed = expectStrEq("  resolved path should be empty", tempBuffer, "") && passed;

	// non-existent prefix, with no pattern
	*tempBuffer = '\0';
	length = 255;
	fr = resolvePath("unknown-path-prefix:", tempBuffer, &length);
	passed = expectEq("resolvePath on unknown-path-prefix: returns FR_NO_PATH", fr, FR_NO_PATH) && passed;
	passed = expectStrEq("  resolved path should be empty", tempBuffer, "") && passed;

	// include a bad path to our search path (this time with just a space separator, and changing comma to semicolon)
	tempString = mos_strdup("bad-test-path/which/doesnt/exist /path-tests-tmp/subdir/; /path-tests-tmp/");
	createOrUpdateSystemVariable("Path-Tests$Path", MOS_VAR_STRING, tempString);

	// prefix-only should match to first valid path
	*tempBuffer = '\0';
	length = 255;
	fr = resolvePath("path-tests:", tempBuffer, &length);
	passed = expectEq("resolvePath on path-tests: returns FR_NO_FILE", fr, FR_NO_FILE) && passed;
	passed = expectStrEq("  resolved path should be /path-tests-tmp/subdir/", tempBuffer, "/path-tests-tmp/subdir/") && passed;

	*tempBuffer = '\0';
	length = 255;
	fr = resolvePath("path-tests:*.txt", tempBuffer, &length);
	passed = expectEq("resolvePath on path-tests:*.txt returns FR_OK", fr, FR_OK) && passed;
	passed = expectEq("  resolved path matches pattern", pmatch("/path-tests-tmp/testfile-*.txt", tempBuffer, MATCH_CASE_INSENSITIVE), 0) && passed;

	if (!passed || verbose) printf("\n\r");	

	// getDirectoryForPath tests
	// getDirectoryForPath should return the directory part of a path
	// and update the length to the length of the directory part
	// it will also take an index for which prefix to use

	length = 255;
	fr = getDirectoryForPath("path-tests:file.txt", tempBuffer, &length, 0);
	passed = expectEq("getDirectoryForPath on path-tests:file.txt returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  directory should be bad-test-path/which/doesnt/exist", tempBuffer, "bad-test-path/which/doesnt/exist") && passed;

	i = strlen(tempBuffer);
	length = 255;
	fr = getDirectoryForPath("path-tests:file.txt", NULL, &length, 0);
	passed = expectEq("getDirectoryForPath on path-tests:file.txt (no target buffer) returns FR_OK", fr, FR_OK) && passed;
	passed = expectEq("  length should match previous result string length", length - 1, i) && passed;

	length = 255;
	fr = getDirectoryForPath("path-tests:file.txt", tempBuffer, &length, (BYTE)1);
	passed = expectEq("getDirectoryForPath on path-tests:file.txt, index 1 returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  directory should be /path-tests-tmp/subdir/", tempBuffer, "/path-tests-tmp/subdir/") && passed;

	length = 255;
	fr = getDirectoryForPath("path-tests:file.txt", tempBuffer, &length, (BYTE)2);
	passed = expectEq("getDirectoryForPath on path-tests:file.txt, index 2 returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  directory should be /path-tests-tmp/", tempBuffer, "/path-tests-tmp/") && passed;

	length = 255;
	fr = getDirectoryForPath("path-tests:file.txt", tempBuffer, &length, (BYTE)3);
	passed = expectEq("getDirectoryForPath on path-tests:file.txt, index 3 returns FR_NO_PATH", fr, FR_NO_PATH) && passed;
	passed = expectStrEq("  directory for index 3 should be empty", tempBuffer, "") && passed;

	length = 255;
	fr = getDirectoryForPath("path-tests:some/dir/file.txt", tempBuffer, &length, (BYTE)2);
	passed = expectEq("getDirectoryForPath on path-tests:some/dir/file.txt, index 2 returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  directory should be /path-tests-tmp/some/dir/", tempBuffer, "/path-tests-tmp/some/dir/") && passed;

	// and test paths without a prefix
	length = 255;
	fr = getDirectoryForPath("file.txt", tempBuffer, &length, 0);
	passed = expectEq("getDirectoryForPath on file.txt returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  directory for path file.txt should be empty", tempBuffer, "") && passed;

	length = 255;
	fr = getDirectoryForPath("/file.txt", tempBuffer, &length, 0);
	passed = expectEq("getDirectoryForPath on /file.txt returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  directory for path /file.txt should be /", tempBuffer, "/") && passed;

	length = 255;
	fr = getDirectoryForPath("/some/dir/file.txt", tempBuffer, &length, 0);
	passed = expectEq("getDirectoryForPath on /some/dir/file.txt returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  directory for path /some/dir/file.txt should be /some/dir/", tempBuffer, "/some/dir/") && passed;

	length = 255;
	fr = getDirectoryForPath("dir/path/only/", tempBuffer, &length, 0);
	passed = expectEq("getDirectoryForPath on dir/path/only/ returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  directory for path dir/path/only/ should match full path", tempBuffer, "dir/path/only/") && passed;

	length = 255;
	fr = getDirectoryForPath("unknown-path-prefix:", tempBuffer, &length, 0);
	passed = expectEq("getDirectoryForPath on unknown-path-prefix: returns FR_NO_PATH", fr, FR_NO_PATH) && passed;
	passed = expectStrEq("  directory for path unknown-path-prefix: should be empty", tempBuffer, "") && passed;

	length = 255;
	fr = getDirectoryForPath("unknown-path-prefix:path/to-file.txt", tempBuffer, &length, 0);
	passed = expectEq("getDirectoryForPath on unknown-path-prefix:path/to/file.txt returns FR_NO_PATH", fr, FR_NO_PATH) && passed;
	passed = expectStrEq("  directory for path unknown-path-prefix:path/to/file.txt should be empty", tempBuffer, "") && passed;


	// Test out the newResolvePath
	// This should be able to resolve paths in a similar way to resolvePath
	// but accept an index and a `DIR` object for search persistence
	// it should automatically skip past non-existent directories in paths, and update index accordingly
	index = 0;
	length = 255;
	fr = newResolvePath("path-tests:file.txt", tempBuffer, &length, &index, &dir);
	passed = expectEq("resolvePath on path-tests:file.txt returns FR_NO_FILE", fr, FR_NO_FILE) && passed;
	passed = expectStrEq("  resolved path should be /path-tests-tmp/subdir/file.txt", tempBuffer, "/path-tests-tmp/subdir/file.txt") && passed;
	passed = expectEq("  index should be 2 (one after what we have resolved to)", index, 2) && passed;
	// if we iterate, then we should get the next directory
	length = 255;
	fr = newResolvePath("path-tests:file.txt", tempBuffer, &length, &index, &dir);
	passed = expectEq("resolvePath on path-tests:file.txt (first iteration) returns FR_NO_FILE", fr, FR_NO_FILE) && passed;
	passed = expectStrEq("  resolved path (iter 1) should be /path-tests-tmp/file.txt", tempBuffer, "/path-tests-tmp/file.txt") && passed;
	passed = expectEq("  index should be 3 (one after what we have resolved to)", index, 3) && passed;
	// and iterate again should return FR_NO_PATH
	length = 255;
	fr = newResolvePath("path-tests:file.txt", tempBuffer, &length, &index, &dir);
	passed = expectEq("resolvePath on path-tests:file.txt returns FR_NO_PATH", fr, FR_NO_PATH) && passed;
	passed = expectStrEq("  resolved path (iter 2) should be empty", tempBuffer, "") && passed;
	passed = expectEq("  index should still be 3 (one after what we have resolved to)", index, 3) && passed;

	length = 255;
	index = 0;

	// iterate over a directory using a wildcard, passing in our result buffer to get next matches
	for (i=0; i<5; i++) {
		length = 255;
		fr = newResolvePath("path-tests:testfile-*.txt", tempBuffer, &length, &index, &dir);
		passed = expectEq("resolvePath check returned pattern match", pmatch("/path-tests-tmp/testfile-*.txt", tempBuffer, MATCH_CASE_INSENSITIVE), 0) && passed;
	}
	passed = expectEq("matchRawPath with pattern returns FR_OK after all files matched", fr, FR_OK) && passed;

	// next match should fail with no path
	length = 255;
	fr = newResolvePath("path-tests:testfile-*.txt", tempBuffer, &length, &index, &dir);
	passed = expectEq("resolvePath with pattern returns FR_NO_PATH after all files matched", fr, FR_NO_PATH) && passed;

	index = 0;
	length = 255;
	fr = newResolvePath("path-tests:", tempBuffer, &length, &index, &dir);
	passed = expectEq("resolvePath on path-tests: returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  resolved path should be /path-tests-tmp/subdir/", tempBuffer, "/path-tests-tmp/subdir/") && passed;
	passed = expectEq("  index should be 2 (one after what we have resolved to)", index, 2) && passed;

	length = 255;
	fr = newResolvePath("/", tempBuffer, &length, NULL, &dir);
	passed = expectEq("resolvePath on / returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  resolved path should be /", tempBuffer, "/") && passed;

	length = 255;
	fr = newResolvePath("/path-tests-tmp", tempBuffer, &length, NULL, &dir);
	passed = expectEq("resolvePath on /path-tests-tmp returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  resolved path should be /path-tests-tmp", tempBuffer, "/path-tests-tmp") && passed;

	length = 255;
	fr = newResolvePath("/path-tests-tmp/", tempBuffer, &length, NULL, &dir);
	passed = expectEq("resolvePath on /path-tests-tmp/ returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  resolved path should be /path-tests-tmp/", tempBuffer, "/path-tests-tmp/") && passed;

	// Check we can resolve with a direct directory
	length = 255;
	index = 0;
	fr = newResolvePath("/path-tests-tmp/testfile-*.txt", tempBuffer, &length, &index, &dir);
	passed = expectEq("resolvePath on /path-tests-tmp/testfile-*.txt returns FR_OK", fr, FR_OK) && passed;
	passed = expectEq("  resolved path matches pattern", pmatch("/path-tests-tmp/testfile-*.txt", tempBuffer, MATCH_CASE_INSENSITIVE), 0) && passed;



	// once tests are complete, delete the test folder
	passed = expectEq("f_unlink of file in a non-existant sub-directory returns FR_NO_PATH", f_unlink("/path-tests-tmp/foo/bar"), FR_NO_PATH) && passed;
	passed = expectEq("f_unlink on non-existent file returns FR_NO_FILE", f_unlink("/path-tests-tmp/zz"), FR_NO_FILE) && passed;

	// Unlink our test files
	for (i=0; i<5; i++) {
		sprintf(tempBuffer, "/path-tests-tmp/testfile-%d.txt", i);
		f_unlink(tempBuffer);
		// passed = expectEq("f_unlink on test file returns FR_OK", f_unlink(tempBuffer), FR_OK) && passed;
	}
	// and our sub-directory
	passed = expectEq("f_unlink on test sub-directory returns FR_OK", f_unlink("/path-tests-tmp/subdir"), FR_OK) && passed;

	passed = expectEq("f_unlink on test directory returns FR_OK", f_unlink("/path-tests-tmp"), FR_OK) && passed;

	if (passed) {
		printf("\n\rAll tests passed!\r\n");
	}

	// Remove our test path sysvar
	mos_cmdUNSET("Path-Tests$Path");
}

#endif /* DEBUG */
