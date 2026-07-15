#ifndef LOWERING_LOCAL_STORAGE_H
#define LOWERING_LOCAL_STORAGE_H

typedef struct psx_lowering_context_t psx_lowering_context_t;

void local_storage_reset(psx_lowering_context_t *context);
void local_storage_reserve_prefix(
    psx_lowering_context_t *context, int bytes);
int local_storage_allocate(
    psx_lowering_context_t *context, int size, int align);

#endif
