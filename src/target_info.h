#ifndef AG_TARGET_INFO_H
#define AG_TARGET_INFO_H

typedef struct {
  int pointer_size;
} ag_target_info_t;

/* Explicit target description owned by a compilation session. */
ag_target_info_t ag_target_info_host(void);
ag_target_info_t ag_target_info_wasm32(void);
int ag_target_info_pointer_size(const ag_target_info_t *target);

/* Compatibility API for context-free callers. New compilation paths must
 * carry ag_target_info_t explicitly. */
int ag_target_pointer_size(void);
void ag_target_set_pointer_size(int size);

#endif
