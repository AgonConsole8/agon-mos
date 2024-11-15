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

	if (!passed) printf("\n\r");

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

	// Add rests for resolvePath
	// which should replace prefix with sysvar-expanded value

	// First we need to set up a path-tests$path sysvar
	// and then we can test resolvePath
	tempString = mos_strdup("/path-tests-tmp/");
	createOrUpdateSystemVariable("Paths-Tests$Path", MOS_VAR_STRING, tempString);

	if (!passed) printf("\n\r");	

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
	mos_cmdUNSET("Paths-Tests$Path");
}

#endif /* DEBUG */
