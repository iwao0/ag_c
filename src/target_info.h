#ifndef AG_TARGET_INFO_H
#define AG_TARGET_INFO_H

/* Translation target ABI shared by frontend, IR lowering, and backends. */
int ag_target_pointer_size(void);
void ag_target_set_pointer_size(int size);

#endif
