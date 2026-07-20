#ifndef PARSER_GVAR_PUBLIC_H
#define PARSER_GVAR_PUBLIC_H

#include "core.h"
#include "init_slot.h"
#include "type.h"
#include "../semantic/record_decl.h"
#include "../semantic/record_layout.h"
#include "../semantic/type_identity.h"

typedef struct global_var_t global_var_t;
typedef struct psx_type_t psx_type_t;
typedef struct psx_semantic_context_t psx_semantic_context_t;
typedef struct psx_record_decl_table_t psx_record_decl_table_t;
typedef struct psx_record_layout_table_t psx_record_layout_table_t;
typedef struct ag_target_info_t ag_target_info_t;
typedef void (*global_var_visitor_t)(global_var_t *gv, void *user);

psx_gvar_init_slot_t ps_gvar_init_slot_view(
    const global_var_t *gv, int idx);
tk_float_kind_t ps_gvar_init_slot_fp_kind(
    const global_var_t *gv, int idx);
int ps_gvar_init_slot_is_plain_zero(
    const global_var_t *gv, int idx);
int ps_gvar_union_init_slot_fp_size(
    const global_var_t *gv, int idx);
int ps_gvar_union_init_slot_ordinal(
    const global_var_t *gv, int idx);
void ps_gvar_init_slots_alloc(
    global_var_t *gv, int cap, int with_fvalues);
void psx_gvar_init_slots_ensure_capacity(
    global_var_t *gv, int *cap, int min_cap);
void psx_gvar_init_slots_pad_zeros(
    global_var_t *gv, int *cap, int total_slots);
int ps_gvar_init_slots_write_string_units(
    global_var_t *gv, int start_idx, const char *str, int len,
    int elem_size, int max_slots);
void ps_gvar_init_slot_clear(global_var_t *gv, int idx);
void ps_gvar_init_slot_write(
    global_var_t *gv, int idx, long long value,
    double fvalue, char *symbol, int symbol_len);
void ps_gvar_init_slot_write_fp_sentinel(
    global_var_t *gv, int idx, tk_float_kind_t fp_kind, int fp_size);
void ps_gvar_init_slot_set_ordinal(
    global_var_t *gv, int idx, int ordinal);

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
  void (*scalar)(void *user, const psx_record_member_decl_t *member,
                 psx_type_id_t value_type_id, int slot, long long offset);
  void (*bitfield_unit)(void *user, const psx_gvar_bitfield_unit_t *unit,
                        long long base_offset);
  void (*bitfield_member)(void *user,
                          const psx_record_member_decl_t *member,
                          const psx_record_member_layout_t *layout,
                          psx_type_id_t value_type_id, int slot,
                          long long offset);
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

int ps_gvar_is_array(const global_var_t *gv);
int ps_gvar_is_tag_aggregate(const global_var_t *gv);
int ps_gvar_is_struct_aggregate(const global_var_t *gv);
int ps_gvar_is_union_aggregate(const global_var_t *gv);
int ps_gvar_is_bool_scalar(const global_var_t *gv);
int ps_gvar_array_element_is_bool(const global_var_t *gv);
int ps_gvar_array_element_count(const global_var_t *gv);
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
ps_gvar_init_member_value(const psx_semantic_type_table_t *semantic_types,
                           const global_var_t *gv, int idx,
                           const psx_record_member_decl_t *member,
                           int member_size);
psx_gvar_init_scalar_value_t
ps_gvar_init_scalar_value(const global_var_t *gv, int fallback_size);
int ps_gvar_visit_initializer_classified(
    const global_var_t *gv, const psx_gvar_initializer_class_t *init_class,
    int scalar_size, int slot_element_size, int slot_element_count,
    const psx_gvar_initializer_visit_ops_t *ops, void *user);
int ps_gvar_fp_bit_pattern(tk_float_kind_t fp_kind, double value,
                            psx_gvar_fp_bits_t *out);
int ps_gvar_symbol_ref_named(psx_gvar_symbol_ref_t ref,
                              char **out_name, int *out_len);
int ps_gvar_symbol_ref_named_function_in(
    psx_semantic_context_t *semantic_context,
    psx_gvar_symbol_ref_t ref, char **out_name, int *out_len);
int ps_gvar_walk_aggregate_initializer_in(
    psx_semantic_context_t *semantic_context,
    global_var_t *gv, long long base_offset,
    const psx_gvar_aggregate_walk_ops_t *ops, void *user);
int ps_gvar_walk_resolved_aggregate_initializer(
    const psx_semantic_type_table_t *semantic_types,
    const psx_record_decl_table_t *record_decls,
    const psx_record_layout_table_t *record_layouts,
    const ag_target_info_t *target, psx_type_id_t root_type_id,
    global_var_t *gv, long long base_offset,
    const psx_gvar_aggregate_walk_ops_t *ops, void *user);
unsigned long long ps_gvar_init_slot_bitfield_bits(const global_var_t *gv, int idx,
                                                    int bit_width, int bit_offset);
int ps_gvar_is_extern_decl(const global_var_t *gv);
int ps_gvar_is_thread_local(const global_var_t *gv);
int ps_gvar_is_static_storage(const global_var_t *gv);
char *ps_gvar_name(const global_var_t *gv);
int ps_gvar_name_len(const global_var_t *gv);
const psx_type_t *ps_gvar_get_decl_type(const global_var_t *gv);
psx_qual_type_t ps_gvar_decl_qual_type(const global_var_t *gv);
psx_type_id_t ps_gvar_decl_type_id(const global_var_t *gv);

#endif
