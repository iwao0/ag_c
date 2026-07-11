#ifndef LOWERING_LOCAL_STORAGE_H
#define LOWERING_LOCAL_STORAGE_H

void local_storage_reset(void);
void local_storage_reserve_prefix(int bytes);
int local_storage_allocate(int size, int align);

#endif
