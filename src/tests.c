#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "umm_malloc.h"
#include "ff.h"

#include "tests.h"

#if DEBUG > 0

#define MG_MAX_ITEMS	64
#define MG_ITERS 1000
struct mg_item_t {
	int *ptr;
	int num;
};

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

void malloc_grind() {
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

void expectEq(char * check, int actual, int expected) {
	bool pass = actual == expected;
	printf("%s: %s", check, pass ? "PASS" : "FAIL");
	if (!pass) {
		printf("\n\r    (expected %d == %d)", expected, actual);
	}
	printf("\n\r");
}

void expectNotEq(char * check, int actual, int expected) {
	bool pass = actual != expected;
	printf("%s: %s", check, pass ? "PASS" : "FAIL");
	if (!pass) {
		printf("\n\r    (expected %d != %d)", expected, actual);
	}
	printf("\n\r");
}

void expectStrEq(char * check, char * actual, char * expected) {
	bool pass = strcmp(actual, expected) == 0;
	printf("%s: %s", check, pass ? "PASS" : "FAIL");
	if (!pass) {
		printf("\n\r    (expected \"%s\" == \"%s\")", expected, actual);
	}
	printf("\n\r");
}

void path_tests() {
	FRESULT fr;
	DIR dir;
	FILINFO fno;

	printf("Perform path tests\r\n");
	printf("Path tests require a working SD card.\r\n");

	// create a test folder, and test files
	// once tests are complete, delete the test folder

	// For now, we're winging it, working with existing directory stuff
	// which will all need to be rewritten

	// check exact behaviour of fatfs APIs on hardware vs emulator

	// what does opendir do when there's no directory
	expectEq("f_opendir on non-existent directory returns FR_NO_PATH", f_opendir(&dir, "non-existent-directory"), FR_NO_PATH);
	f_closedir(&dir);

	// what does findfirst do when there's no directory?
	expectEq("f_findfirst on non-existent directory returns FR_NO_PATH", f_findfirst(&dir, &fno, "non-existent-directory", "*"), FR_NO_PATH);
	f_closedir(&dir);

	// what does findfirst do when there's no leafname/pattern (empty string)
	expectEq("f_findfirst empty pattern on valid directory returns FR_OK", f_findfirst(&dir, &fno, "/mos/", ""), FR_OK);
	f_closedir(&dir);

	expectEq("f_findfirst valid pattern on valid directory returns FR_OK", f_findfirst(&dir, &fno, "/mos/", "*"), FR_OK);
	f_closedir(&dir);
	expectNotEq("  the returned filename should not be empty", fno.fname[0], 0);

	// what does findfirst do when there's no matching pattern
	expectEq("f_findfirst on valid directory non-existent file returns FR_OK", f_findfirst(&dir, &fno, "/mos/", "non-existent-file"), FR_OK);
	f_closedir(&dir);
	expectStrEq("  the returned filename should be empty", fno.fname, "");

	// check what f_stat returns - first of all when there's no pattern
	expectEq("f_stat on non-existent directory returns FR_NO_PATH", f_stat("/non-existent-directory/file", &fno), FR_NO_PATH);
	f_closedir(&dir);
	expectStrEq("  the returned filename should be empty", fno.fname, "");

	expectEq("f_stat on non-existent file returns FR_NO_FILE", f_stat("/mos/non-existent-file", &fno), FR_NO_FILE);
	f_closedir(&dir);
	expectStrEq("  the returned filename should be empty", fno.fname, "");

	// what does f_stat do when there is a pattern?
	expectEq("f_stat on non-existent directory with leaf pattern returns FR_NO_PATH", f_stat("/non-existent-directory/*", &fno), FR_NO_PATH);
	f_closedir(&dir);
	expectStrEq("  the returned filename should be empty", fno.fname, "");

	expectEq("f_stat on valid directory with a leaf pattern returns FR_INVALID_NAME", f_stat("/mos/*", &fno), FR_INVALID_NAME);
	f_closedir(&dir);
	expectStrEq("  the returned filename should be empty", fno.fname, "");
}

#endif /* DEBUG */
