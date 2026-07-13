#ifndef PARSER_GVAR_PUBLIC_H
#define PARSER_GVAR_PUBLIC_H

#include "core.h"
#include "tag_member_public.h"

typedef struct global_var_t global_var_t;

typedef enum {
  PSX_GVAR_INIT_KIND_INTEGER = 0,
  PSX_GVAR_INIT_KIND_AGGREGATE,
  PSX_GVAR_INIT_KIND_SYMBOL,
  PSX_GVAR_INIT_KIND_SLOTS,
  PSX_GVAR_INIT_KIND_FLOAT,
} psx_gvar_init_kind_t;

typedef struct {
  psx_gvar_init_kind_t kind;
  int is_tag_aggregate;
  int has_aggregate_initializer;
  int has_explicit_initializer;
  int has_payload;
} psx_gvar_initializer_class_t;

typedef struct {
  int elem_size;
  int elem_count;
  int init_count;
  int is_fp_array;
  tk_float_kind_t fp_kind;
} psx_gvar_init_slots_layout_t;

typedef enum {
  PSX_GVAR_INIT_VALUE_INTEGER = 0,
  PSX_GVAR_INIT_VALUE_SYMBOL,
  PSX_GVAR_INIT_VALUE_FLOAT,
} psx_gvar_init_value_kind_t;

typedef struct {
  unsigned long long bits;
  int size;
} psx_gvar_fp_bits_t;

typedef enum {
  PSX_GVAR_SYMBOL_REF_NONE = 0,
  PSX_GVAR_SYMBOL_REF_STRING_LITERAL,
  PSX_GVAR_SYMBOL_REF_NAMED,
} psx_gvar_symbol_ref_kind_t;

typedef struct {
  psx_gvar_symbol_ref_kind_t kind;
  char *symbol;
  int symbol_len;
  long long addend;
} psx_gvar_symbol_ref_t;

typedef struct {
  psx_gvar_init_value_kind_t kind;
  psx_gvar_symbol_ref_t symbol_ref;
  long long value;
  double fvalue;
  tk_float_kind_t fp_kind;
  int size;
} psx_gvar_init_value_t;

typedef psx_gvar_init_value_t psx_gvar_init_slot_value_t;
typedef psx_gvar_init_value_t psx_gvar_init_member_value_t;
typedef psx_gvar_init_value_t psx_gvar_init_scalar_value_t;

typedef struct {
  int offset;
  int size;
  int last_member_index;
  unsigned long long packed;
} psx_gvar_bitfield_unit_t;

typedef struct {
  void (*scalar)(void *user, const tag_member_info_t *mi, int slot, long long offset);
  void (*bitfield_unit)(void *user, const psx_gvar_bitfield_unit_t *unit,
                        long long base_offset);
  void (*bitfield_member)(void *user, const tag_member_info_t *mi, int slot,
                          long long base_offset);
  void (*padding)(void *user, long long offset, int size);
} psx_gvar_aggregate_walk_ops_t;

typedef int (*psx_gvar_init_slot_value_fn)(
    void *user, int index, psx_gvar_init_slot_value_t value,
    const psx_gvar_init_slots_layout_t *layout);

typedef struct {
  int (*aggregate)(void *user, const psx_gvar_initializer_class_t *init_class);
  int (*slots)(void *user, const psx_gvar_init_slots_layout_t *layout,
               const psx_gvar_initializer_class_t *init_class);
  int (*scalar)(void *user, psx_gvar_init_scalar_value_t value,
                const psx_gvar_initializer_class_t *init_class);
} psx_gvar_initializer_visit_ops_t;

global_var_t *ps_find_global_var(char *name, int len);
int ps_gvar_decl_sizeof(const global_var_t *gv, int fallback_size);
int ps_gvar_storage_size(const global_var_t *gv, int fallback_size);
int ps_gvar_is_array(const global_var_t *gv);
int ps_gvar_is_tag_aggregate(const global_var_t *gv);
int ps_gvar_is_struct_aggregate(const global_var_t *gv);
int ps_gvar_is_union_aggregate(const global_var_t *gv);
int ps_gvar_is_bool_scalar(const global_var_t *gv);
int ps_gvar_array_element_is_bool(const global_var_t *gv);
int ps_gvar_array_element_size(const global_var_t *gv);
int ps_gvar_array_element_count(const global_var_t *gv);
int ps_gvar_initializer_element_size(const global_var_t *gv, int fallback_size);
int ps_gvar_initializer_element_count(const global_var_t *gv, int fallback_size);
int ps_gvar_has_aggregate_initializer(const global_var_t *gv);
int ps_gvar_has_explicit_initializer(const global_var_t *gv);
psx_gvar_initializer_class_t
ps_gvar_initializer_class(const global_var_t *gv, int include_empty_aggregate);
int ps_gvar_walk_init_slot_values(const global_var_t *gv,
                                   const psx_gvar_init_slots_layout_t *layout,
                                   int value_count,
                                   psx_gvar_init_slot_value_fn callback,
                                   void *user);
psx_gvar_init_member_value_t
ps_gvar_init_member_value(const global_var_t *gv, int idx,
                           const tag_member_info_t *member);
psx_gvar_init_scalar_value_t
ps_gvar_init_scalar_value(const global_var_t *gv, int fallback_size);
int ps_gvar_visit_initializer_classified(
    const global_var_t *gv, const psx_gvar_initializer_class_t *init_class,
    int fallback_size, const psx_gvar_initializer_visit_ops_t *ops, void *user);
int ps_gvar_visit_initializer(const global_var_t *gv, int include_empty_aggregate,
                               int fallback_size,
                               const psx_gvar_initializer_visit_ops_t *ops,
                               void *user);
int ps_gvar_fp_bit_pattern(tk_float_kind_t fp_kind, double value,
                            psx_gvar_fp_bits_t *out);
int ps_gvar_symbol_ref_named(psx_gvar_symbol_ref_t ref,
                              char **out_name, int *out_len);
int ps_gvar_symbol_ref_named_function(psx_gvar_symbol_ref_t ref,
                                       char **out_name, int *out_len);
int ps_gvar_init_value_named_function(psx_gvar_init_value_t value,
                                        char **out_name, int *out_len);
int ps_gvar_walk_aggregate_initializer(global_var_t *gv, long long base_offset,
                                        const psx_gvar_aggregate_walk_ops_t *ops,
                                        void *user);
unsigned long long ps_gvar_init_slot_bitfield_bits(const global_var_t *gv, int idx,
                                                    int bit_width, int bit_offset);
int ps_gvar_is_extern_decl(const global_var_t *gv);
int ps_gvar_is_thread_local(const global_var_t *gv);
int ps_gvar_is_static_storage(const global_var_t *gv);
int ps_gvar_is_extern_decl_by_name(char *name, int len);
int ps_gvar_is_thread_local_by_name(char *name, int len);
int ps_gvar_is_static_storage_by_name(char *name, int len);
char *ps_gvar_name(const global_var_t *gv);
int ps_gvar_name_len(const global_var_t *gv);
psx_decl_funcptr_sig_t ps_gvar_funcptr_sig(const global_var_t *src);
psx_decl_funcptr_sig_t ps_gvar_funcptr_sig_by_name(char *name, int len);
void ps_gvar_get_funcptr_sig(const global_var_t *src,
                             psx_decl_funcptr_sig_t *out);
void ps_gvar_get_funcptr_sig_by_name(char *name, int len,
                                     psx_decl_funcptr_sig_t *out);

#endif
