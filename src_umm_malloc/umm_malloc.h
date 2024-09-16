/* ----------------------------------------------------------------------------
 * umm_malloc.h - a memory allocator for embedded systems (microcontrollers)
 *
 * See copyright notice in LICENSE.TXT
 * ----------------------------------------------------------------------------
 */

#ifndef UMM_MALLOC_H
#define UMM_MALLOC_H

#include <defines.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */

typedef struct umm_heap_config {
    void *pheap;
    UINT24 heap_size;
    UINT16 numblocks;
} umm_heap;

extern void  umm_multi_init_heap(umm_heap *heap, void *ptr, UINT24 size);

extern void *umm_multi_malloc(umm_heap *heap, UINT24 size);
extern void *umm_multi_calloc(umm_heap *heap, UINT24 num, UINT24 size);
extern void *umm_multi_realloc(umm_heap *heap, void *ptr, UINT24 size);
extern void  umm_multi_free(umm_heap *heap, void *ptr);

/* ------------------------------------------------------------------------ */

extern void  umm_init_heap(void *ptr, UINT24 size);

extern void *umm_malloc(UINT24 size);
extern void *umm_calloc(UINT24 num, UINT24 size);
extern void *umm_realloc(void *ptr, UINT24 size);
extern void  umm_free(void *ptr);

/* ------------------------------------------------------------------------ */

#ifdef __cplusplus
}
#endif

#endif /* UMM_MALLOC_H */
