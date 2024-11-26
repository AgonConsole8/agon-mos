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

	// printf("Native fatfs function tests done.\r\n");

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

	// First we need to set up a path-tests$path sysvar
	// and then we can test resolvePath
	tempString = mos_strdup("/path-tests-tmp/");
	createOrUpdateSystemVariable("Path-Tests$Path", MOS_VAR_STRING, tempString);

	// use getResolvedPath to resolve our path into tempString - single test only, heavy lifting done by resolvePath tested below
	fr = getResolvedPath("path-tests:file.txt", &tempString);
	passed = expectEq("getResolvedPath on path-tests:file.txt returns FR_NO_FILE", fr, FR_NO_FILE) && passed;
	passed = expectStrEq("  getResolvedPath, path should be /path-tests-tmp/file.txt", tempString, "/path-tests-tmp/file.txt") && passed;
	umm_free(tempString);

	// include a bad path to our search path (this time with just a space separator, and changing comma to semicolon)
	tempString = mos_strdup("bad-test-path/which/doesnt/exist /path-tests-tmp/subdir/; /path-tests-tmp/");
	createOrUpdateSystemVariable("Path-Tests$Path", MOS_VAR_STRING, tempString);

	// getDirectoryForPath tests
	// getDirectoryForPath should return the directory part of a path
	// and update the length to the length of the directory part
	// it will also take an index for which prefix to use

	// printf("Running getDirectoryForPath tests...\r\n");

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
	fr = getDirectoryForPath("path-tests:file.txt", NULL, &length, (BYTE)3);
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

	// printf("Done with getDirectoryForPath tests...\r\n");

	// Test resolvePath
	// it should automatically skip past non-existent directories in paths, and update index accordingly
	// index and dir are optional, and can be NULL, but when present will be used to iterate over directories/matches

	f_closedir(&dir);
	index = 0;
	length = 0;
	fr = resolvePath("path-tests:file.txt", NULL, &length, &index, &dir);
	passed = expectEq("resolvePath (fetching length) on path-tests:file.txt returns FR_NO_FILE", fr, FR_NO_FILE) && passed;
	// Keeping index at 0 is not implemented
	// passed = expectEq("  index should remain at 0", index, 0) && passed;
	passed = expectEq("  length should be match or be length of /path-tests-tmp/subdir/file.txt", length, strlen("/path-tests-tmp/subdir/file.txt") + 1) && passed;

	index = 0;
	length = 0;
	fr = resolvePath("path-tests:file.txt", NULL, &length, &index, NULL);
	passed = expectEq("resolvePath (fetching length, null dir) on path-tests:file.txt returns FR_NO_FILE", fr, FR_NO_FILE) && passed;
	// passed = expectEq("  index should remain at 0", index, 0) && passed;
	passed = expectEq("  length should be match or be length of /path-tests-tmp/subdir/file.txt", length, strlen("/path-tests-tmp/subdir/file.txt") + 1) && passed;

	length = 0;
	fr = resolvePath("path-tests:file.txt", NULL, &length, NULL, NULL);
	passed = expectEq("resolvePath (fetching length, no index or dir) on path-tests:file.txt returns FR_NO_FILE", fr, FR_NO_FILE) && passed;
	passed = expectEq("  length should be long enough (resolvePath, no index or dir)", length, strlen("/path-tests-tmp/subdir/file.txt") + 1) && passed;

	index = 0;
	length = 255;
	fr = resolvePath("path-tests:file.txt", tempBuffer, &length, &index, &dir);
	passed = expectEq("resolvePath on path-tests:file.txt returns FR_NO_FILE", fr, FR_NO_FILE) && passed;
	passed = expectStrEq("  resolved path should be /path-tests-tmp/subdir/file.txt", tempBuffer, "/path-tests-tmp/subdir/file.txt") && passed;
	passed = expectEq("  index should be 2 (one after what we have resolved to)", index, 2) && passed;
	// if we iterate, then we should get the next directory
	length = 255;
	fr = resolvePath("path-tests:file.txt", tempBuffer, &length, &index, &dir);
	passed = expectEq("resolvePath on path-tests:file.txt (first iteration) returns FR_NO_FILE", fr, FR_NO_FILE) && passed;
	passed = expectStrEq("  resolved path (iter 1) should be /path-tests-tmp/file.txt", tempBuffer, "/path-tests-tmp/file.txt") && passed;
	passed = expectEq("  index should be 3 (one after what we have resolved to)", index, 3) && passed;
	// and iterate again should return FR_NO_PATH
	length = 255;
	fr = resolvePath("path-tests:file.txt", tempBuffer, &length, &index, &dir);
	passed = expectEq("resolvePath on path-tests:file.txt returns FR_NO_PATH", fr, FR_NO_PATH) && passed;
	passed = expectStrEq("  resolved path (iter 2) should be empty", tempBuffer, "") && passed;
	passed = expectEq("  index should still be 3 (one after what we have resolved to)", index, 3) && passed;

	index = 0;
	length = 255;
	fr = resolvePath("path-tests:subdir/file.txt", tempBuffer, &length, &index, &dir);
	passed = expectEq("resolvePath on path-tests:subdir/file.txt returns FR_NO_FILE", fr, FR_NO_FILE) && passed;
	passed = expectStrEq("  resolved path (with subdir) should be /path-tests-tmp/subdir/file.txt", tempBuffer, "/path-tests-tmp/subdir/file.txt") && passed;
	passed = expectEq("  index should be 3 (one after what we have resolved to)", index, 3) && passed;

	length = 0;
	fr = resolvePath("path-tests:subdir/file.txt", NULL, &length, NULL, NULL);
	passed = expectEq("resolvePath on path-tests:subdir/file.txt returns FR_NO_FILE", fr, FR_NO_FILE) && passed;
	passed = expectEq("  resolved length should match /path-tests-tmp/subdir/file.txt", length, strlen("/path-tests-tmp/subdir/file.txt") + 1) && passed;

	index = 0;
	// iterate over a directory using a wildcard, passing in our result buffer to get next matches
	for (i=0; i<5; i++) {
		length = 255;
		fr = resolvePath("path-tests:testfile-*.txt", tempBuffer, &length, &index, &dir);
		// printf("  %d: %s\n", i, tempBuffer);
		passed = expectEq("resolvePath check returned pattern match", pmatch("/path-tests-tmp/testfile-*.txt", tempBuffer, MATCH_CASE_INSENSITIVE), 0) && passed;
	}
	passed = expectEq("matchRawPath with pattern returns FR_OK after all files matched", fr, FR_OK) && passed;

	// next match should fail with no path
	length = 255;
	fr = resolvePath("path-tests:testfile-*.txt", tempBuffer, &length, &index, &dir);
	passed = expectEq("resolvePath with pattern returns FR_NO_PATH after all files matched", fr, FR_NO_PATH) && passed;

	index = 0;
	length = 255;
	fr = resolvePath("path-tests:", tempBuffer, &length, &index, &dir);
	passed = expectEq("resolvePath on path-tests: returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  resolved path should be /path-tests-tmp/subdir/", tempBuffer, "/path-tests-tmp/subdir/") && passed;
	passed = expectEq("  index should be 2 (one after what we have resolved to)", index, 2) && passed;

	length = 0;
	fr = resolvePath("path-tests:subdir", NULL, &length, NULL, NULL);
	passed = expectEq("resolvePath on path-tests:subdir returns FR_OK", fr, FR_OK) && passed;
	passed = expectEq("  length should match length of /path-tests-tmp/subdir", length, strlen("/path-tests-tmp/subdir") + 1) && passed;

	index = 0;
	length = 255;
	fr = resolvePath("path-tests:unknown-dir/file.txt", tempBuffer, &length, &index, &dir);
	passed = expectEq("resolvePath on path-tests:unknown-dir/file.txt returns FR_NO_PATH", fr, FR_NO_PATH) && passed;
	passed = expectStrEq("  resolved path for unknown subdir with file should be empty", tempBuffer, "") && passed;

	index = 0;
	length = 255;
	fr = resolvePath("path-tests:unknown-dir/", tempBuffer, &length, &index, &dir);
	passed = expectEq("resolvePath on path-tests:unknown-dir/ returns FR_NO_PATH", fr, FR_NO_PATH) && passed;
	passed = expectStrEq("  resolved path for unknown subdir (no file) should be empty", tempBuffer, "") && passed;

	index = 0;
	length = 255;
	fr = resolvePath("unknown-prefix:", NULL, &length, NULL, NULL);
	passed = expectEq("resolvePath on unknown-prefix: returns FR_NO_PATH", fr, FR_NO_PATH) && passed;

	index = 0;
	length = 255;
	fr = resolvePath("unknown-prefix:with-dir/", NULL, &length, NULL, NULL);
	passed = expectEq("resolvePath on unknown-prefix:with-dir/ returns FR_NO_PATH", fr, FR_NO_PATH) && passed;

	index = 0;
	length = 255;
	fr = resolvePath("unknown-prefix:file.txt", NULL, &length, NULL, NULL);
	passed = expectEq("resolvePath on unknown-prefix:file.txt returns FR_NO_PATH", fr, FR_NO_PATH) && passed;

	index = 0;
	length = 255;
	fr = resolvePath("/path-tests-tmp/unknown-dir/file.txt", tempBuffer, &length, &index, NULL);
	passed = expectEq("resolvePath on path-tests:unknown-dir/file.txt returns FR_NO_PATH", fr, FR_NO_PATH) && passed;
	passed = expectStrEq("  resolved path for unknown subdir 3 should be empty", tempBuffer, "") && passed;
	passed = expectEq("  index for path non-prefixed should remain 0", index, 0) && passed;

	index = 0;
	length = 255;
	fr = resolvePath("/path-tests-tmp/unknown-dir/", NULL, &length, &index, NULL);
	passed = expectEq("resolvePath on path-tests:unknown-dir/ returns FR_NO_PATH", fr, FR_NO_PATH) && passed;
	passed = expectEq("  index for path non-prefixed should remain 0", index, 0) && passed;

	// check index is still at zero

	length = 255;
	fr = resolvePath("/", tempBuffer, &length, NULL, &dir);
	passed = expectEq("resolvePath on / returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  resolved path should be /", tempBuffer, "/") && passed;

	length = 255;
	fr = resolvePath(".", tempBuffer, &length, NULL, NULL);
	passed = expectEq("resolvePath on . returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  resolved path should be .", tempBuffer, ".") && passed;

	length = 255;
	fr = resolvePath("..", tempBuffer, &length, NULL, NULL);
	passed = expectEq("resolvePath on .. returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  resolved path should be ..", tempBuffer, "..") && passed;

	length = 255;
	fr = resolvePath("./..", tempBuffer, &length, NULL, NULL);
	passed = expectEq("resolvePath on ./.. returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  resolved path should be ./..", tempBuffer, "./..") && passed;

	length = 255;
	fr = resolvePath("../../../", tempBuffer, &length, NULL, NULL);
	passed = expectEq("resolvePath on ../../../ returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  resolved path should be ../../../", tempBuffer, "../../../") && passed;

	// TODO change directory to a subdir and test resolvePath ../path-tests-tmp

	length = 255;
	fr = resolvePath("/path-tests-tmp", tempBuffer, &length, NULL, &dir);
	passed = expectEq("resolvePath on /path-tests-tmp returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  resolved path should be /path-tests-tmp", tempBuffer, "/path-tests-tmp") && passed;

	length = 255;
	fr = resolvePath("/path-tests-tmp/", tempBuffer, &length, NULL, &dir);
	passed = expectEq("resolvePath on /path-tests-tmp/ returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  resolved path should be /path-tests-tmp/", tempBuffer, "/path-tests-tmp/") && passed;

	// Check we can resolve with a direct directory
	length = 255;
	index = 0;
	fr = resolvePath("/path-tests-tmp/testfile-*.txt", tempBuffer, &length, &index, &dir);
	passed = expectEq("resolvePath on /path-tests-tmp/testfile-*.txt returns FR_OK", fr, FR_OK) && passed;
	passed = expectEq("  resolved path matches pattern", pmatch("/path-tests-tmp/testfile-*.txt", tempBuffer, MATCH_CASE_INSENSITIVE), 0) && passed;
	// When a match is found, index will go to 1
	// this is harmless for non-prefix matches, as long as there's an accompanying persistent DIR object
	passed = expectEq("  index for path (with pattern) non-prefixed will go to 1", index, 1) && passed;

	fr = resolvePath("/path-tests-tmp/testfile-*.txt", tempBuffer, &length, &index, &dir);
	passed = expectEq("resolvePath on /path-tests-tmp/testfile-*.txt second call returns FR_OK", fr, FR_OK) && passed;
	passed = expectEq("  resolved (repeat) path matches pattern", pmatch("/path-tests-tmp/testfile-*.txt", tempBuffer, MATCH_CASE_INSENSITIVE), 0) && passed;
	passed = expectEq("  index for path (with pattern) non-prefixed stays at 1", index, 1) && passed;

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

void string_tests(bool verbose) {
	bool passed = true;
	char * source;
	char * end;
	char * divider;
	char * result;
	int fr;

	source = umm_malloc(256);

	sprintf(source, "  \"  foo  bar  \"  ");
	fr = extractString(source, &end, NULL, &result, EXTRACT_FLAG_AUTO_TERMINATE);
	passed = expectEq("extractString on source returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  result should be '  foo  bar  '", result, "  foo  bar  ") && passed;
	passed = expectEq("  end should be at end of string", (int)end - 1, (int)source + strlen(source)) && passed;
	passed = expectEq("  end (1) should be a space char", (int)(*end), (int)' ') && passed;

	sprintf(source, "  \"  foo  bar  \"  ");
	fr = extractString(source, &end, NULL, &result, EXTRACT_FLAG_AUTO_TERMINATE | EXTRACT_FLAG_INCLUDE_QUOTES);
	passed = expectEq("extractString on source (include quotes) returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  result should be \"  foo  bar  \"", result, "\"  foo  bar  \"") && passed;
	passed = expectEq("  end should be at end of string", (int)end - 1, (int)source + strlen(source)) && passed;
	passed = expectEq("  end (2) should be a space char", (int)(*end), (int)' ') && passed;

	sprintf(source, "  \"  foo  bar  \"");
	fr = extractString(source, &end, NULL, &result, EXTRACT_FLAG_AUTO_TERMINATE | EXTRACT_FLAG_INCLUDE_QUOTES);
	passed = expectEq("extractString on source (no extra space) returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  result should be \"  foo  bar  \"", result, "\"  foo  bar  \"") && passed;
	passed = expectEq("  end (3) should be a null char", (int)(*end), (int)'\0') && passed;

	sprintf(source, "  \"  foo  bar  \"broken  ");
	fr = extractString(source, &end, NULL, &result, EXTRACT_FLAG_AUTO_TERMINATE | EXTRACT_FLAG_INCLUDE_QUOTES);
	passed = expectEq("extractString on broken source returns MOS_BAD_STRING", fr, MOS_BAD_STRING) && passed;

	sprintf(source, "  \"  foo  bar  broken  ");
	fr = extractString(source, &end, NULL, &result, EXTRACT_FLAG_AUTO_TERMINATE | EXTRACT_FLAG_INCLUDE_QUOTES);
	passed = expectEq("extractString on source with no close quote returns MOS_BAD_STRING", fr, MOS_BAD_STRING) && passed;

	sprintf(source, "  \"  foo \"\" bar  \"  ");
	fr = extractString(source, &end, NULL, &result, EXTRACT_FLAG_AUTO_TERMINATE | EXTRACT_FLAG_INCLUDE_QUOTES);
	passed = expectEq("extractString on source returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  result should be \"  foo \"\" bar  \"", result, "\"  foo \"\" bar  \"") && passed;
	passed = expectEq("  end (with escape 1) should be at end of string", (int)end - 1, (int)source + strlen(source)) && passed;

	sprintf(source, "  \"  foo \\\" bar  \"  ");
	fr = extractString(source, &end, NULL, &result, EXTRACT_FLAG_AUTO_TERMINATE | EXTRACT_FLAG_INCLUDE_QUOTES);
	passed = expectEq("extractString on source returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  result should be \"  foo \\\" bar  \"", result, "\"  foo \\\" bar  \"") && passed;
	passed = expectEq("  end (with escape 2) should be at end of string", (int)end - 1, (int)source + strlen(source)) && passed;

	sprintf(source, "  \"  foo  bar  \"  ");
	fr = extractString(source, &end, NULL, &result, 0);
	passed = expectEq("extractString on source returns FR_OK", fr, FR_OK) && passed;
	// we're not auto-terminating, and not including quotes, so end should point to the closing quote
	passed = expectEq("  end with no terminate should be a quote char", (int)(*end), (int)'"') && passed;

	sprintf(source, "\"test.obey\" 1 2 3 4");
	fr = extractString(source, &end, NULL, &result, EXTRACT_FLAG_AUTO_TERMINATE);
	passed = expectEq("extractString on source with divider returns FR_OK", fr, FR_OK) && passed;
	passed = expectStrEq("  result should be 'test.obey'", result, "test.obey") && passed;
	passed = expectStrEq("  end should point to ' 1 2 3 4'", end, " 1 2 3 4") && passed;

	if (passed) {
		printf("\n\rAll tests passed!\r\n");
	}

	umm_free(source);
}

#endif /* DEBUG */
