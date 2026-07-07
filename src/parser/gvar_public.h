#ifndef PARSER_GVAR_PUBLIC_H
#define PARSER_GVAR_PUBLIC_H

#include "core.h"
#include "init_slot.h"
#include "semantic_public.h"
#include "tag_flat_cover.h"

typedef struct global_var_t global_var_t;

typedef struct {
  char *name;
  int name_len;
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int type_size;
  int init_count;
  int has_init;
  long long init_val;
  char *init_symbol;
  int init_symbol_len;
  long long init_symbol_offset;
  double fval;
  tk_float_kind_t fp_kind;
  int is_array;
  int is_extern_decl;
  int is_static;
  int is_thread_local;
  int is_tag_pointer;
  int has_init_fvalues;
} psx_gvar_view_t;

typedef struct {
  const global_var_t *gv;
  int index;
  int count;
} psx_gvar_init_cursor_t;

typedef struct {
  int offset;
  int size;
  int last_member_index;
  unsigned long long packed;
} psx_gvar_bitfield_unit_t;

typedef struct {
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int type_size;
  int elem_size;
  int elem_count;
  int is_array;
  int is_union;
} psx_gvar_aggregate_layout_t;

typedef struct {
  token_kind_t tag_kind;
  char *tag_name;
  int tag_len;
  int ordinal;
  int count;
  psx_tag_flat_cover_state_t cover_state;
} psx_gvar_aggregate_member_iter_t;

typedef struct {
  void (*scalar)(void *user, const tag_member_info_t *mi, int slot, long long offset);
  void (*bitfield_unit)(void *user, const psx_gvar_bitfield_unit_t *unit,
                        long long base_offset);
  void (*bitfield_member)(void *user, const tag_member_info_t *mi, int slot,
                          long long base_offset);
  void (*padding)(void *user, long long offset, int size);
} psx_gvar_aggregate_walk_ops_t;

global_var_t *psx_find_global_var(char *name, int len);
psx_gvar_view_t psx_gvar_view(const global_var_t *gv);
psx_gvar_aggregate_layout_t psx_gvar_aggregate_layout(const global_var_t *gv);
psx_gvar_aggregate_member_iter_t psx_gvar_aggregate_member_iter(token_kind_t tag_kind,
                                                                char *tag_name,
                                                                int tag_len);
int psx_gvar_aggregate_member_next(psx_gvar_aggregate_member_iter_t *iter,
                                   tag_member_info_t *out, int *out_ordinal);
void psx_gvar_aggregate_member_iter_set_next(psx_gvar_aggregate_member_iter_t *iter,
                                             int next_ordinal);
int psx_gvar_walk_struct_initializer(token_kind_t tag_kind, char *tag_name, int tag_len,
                                     global_var_t *gv, psx_gvar_init_cursor_t *cur,
                                     long long base_offset, int struct_size,
                                     const psx_gvar_aggregate_walk_ops_t *ops,
                                     void *user);
int psx_gvar_walk_union_initializer(token_kind_t tag_kind, char *tag_name, int tag_len,
                                    global_var_t *gv, psx_gvar_init_cursor_t *cur,
                                    long long base_offset, int union_size,
                                    const psx_gvar_aggregate_walk_ops_t *ops,
                                    void *user);
int psx_gvar_walk_aggregate_initializer(global_var_t *gv, long long base_offset,
                                        const psx_gvar_aggregate_walk_ops_t *ops,
                                        void *user);
psx_gvar_init_cursor_t psx_gvar_init_cursor(const global_var_t *gv);
psx_gvar_init_cursor_t psx_gvar_init_cursor_at(const global_var_t *gv, int index);
int psx_gvar_init_cursor_has(const psx_gvar_init_cursor_t *cur);
int psx_gvar_init_cursor_index(const psx_gvar_init_cursor_t *cur);
int psx_gvar_init_cursor_advance(psx_gvar_init_cursor_t *cur);
psx_gvar_init_slot_t psx_gvar_init_cursor_slot(const psx_gvar_init_cursor_t *cur);
int psx_gvar_init_cursor_consume_plain_zero_padding(psx_gvar_init_cursor_t *cur,
                                                    int start_idx, int target_slots);
int psx_gvar_init_cursor_consume_tag_zero_padding(token_kind_t tag_kind, char *tag_name,
                                                  int tag_len,
                                                  psx_gvar_init_cursor_t *cur,
                                                  int start_idx);
unsigned long long psx_gvar_init_slot_bitfield_bits(const global_var_t *gv, int idx,
                                                    int bit_width, int bit_offset);
int psx_gvar_init_cursor_pack_bitfield_unit(token_kind_t tag_kind, char *tag_name, int tag_len,
                                            int member_index, psx_gvar_init_cursor_t *cur,
                                            psx_gvar_bitfield_unit_t *out);
int psx_gvar_is_extern_decl(const global_var_t *gv);
int psx_gvar_is_thread_local(const global_var_t *gv);
int psx_gvar_is_static_storage(const global_var_t *gv);
int psx_gvar_is_extern_decl_by_name(char *name, int len);
int psx_gvar_is_thread_local_by_name(char *name, int len);
int psx_gvar_is_static_storage_by_name(char *name, int len);
char *psx_gvar_name(const global_var_t *gv);
int psx_gvar_name_len(const global_var_t *gv);
psx_decl_funcptr_sig_t psx_gvar_funcptr_sig(const global_var_t *src);
psx_decl_funcptr_sig_t psx_gvar_funcptr_sig_by_name(char *name, int len);

#endif
