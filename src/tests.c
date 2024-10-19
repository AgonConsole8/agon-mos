#include "tests.h"
#include "umm_malloc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

static BOOL malloc_grind_validate(struct mg_item_t *item)
{
	int i;
	for (i=0; i<item->num; i++) {
		if (item->ptr[i] != (int)item->ptr) {
			return 0;
		}
	}
	return 1;
}

static void malloc_grind()
{
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

int mos_cmdTEST(char *ptr)
{
	malloc_grind();
	return 0;
}

#endif /* DEBUG */
