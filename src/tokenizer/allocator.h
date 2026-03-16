#ifndef TOKENIZER_ALLOCATOR_H
#define TOKENIZER_ALLOCATOR_H

#include <stddef.h>

void *tk_allocator_calloc(size_t n, size_t size);
size_t tk_allocator_total_chunks(void);
size_t tk_allocator_total_reserved_bytes(void);

#endif
