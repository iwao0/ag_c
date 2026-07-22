#ifndef AG_TARGET_INFO_H
#define AG_TARGET_INFO_H

typedef enum ag_target_scalar_kind_t {
  AG_TARGET_SCALAR_CHAR,
  AG_TARGET_SCALAR_SHORT,
  AG_TARGET_SCALAR_INT,
  AG_TARGET_SCALAR_LONG,
  AG_TARGET_SCALAR_LONG_LONG,
  AG_TARGET_SCALAR_FLOAT,
  AG_TARGET_SCALAR_DOUBLE,
  AG_TARGET_SCALAR_LONG_DOUBLE,
  AG_TARGET_SCALAR_FLOAT_COMPLEX,
  AG_TARGET_SCALAR_DOUBLE_COMPLEX,
  AG_TARGET_SCALAR_LONG_DOUBLE_COMPLEX,
  AG_TARGET_SCALAR_COUNT,
} ag_target_scalar_kind_t;

typedef struct ag_target_scalar_layout_t {
  int size;
  int alignment;
} ag_target_scalar_layout_t;

typedef enum ag_target_call_abi_t {
  AG_TARGET_CALL_ABI_INVALID = -1,
  AG_TARGET_CALL_ABI_AAPCS64,
  AG_TARGET_CALL_ABI_WASM32,
} ag_target_call_abi_t;

/* C object representation only. Calling convention selection is deliberately
 * kept in TargetInfo so layout caches do not depend on ABI policy. */
typedef struct ag_data_layout_t {
  int pointer_size;
  int pointer_alignment;
  int atomic_promoted_max_size;
  int atomic_max_alignment;
  ag_target_scalar_layout_t scalar[AG_TARGET_SCALAR_COUNT];
} ag_data_layout_t;

typedef struct ag_target_info_t {
  ag_data_layout_t data_layout;
  ag_target_call_abi_t call_abi;
} ag_target_info_t;

/* Explicit target description owned by a compilation session. */
ag_target_info_t ag_target_info_host(void);
ag_target_info_t ag_target_info_wasm32(void);
/* Query APIs never substitute host layout for a missing target. */
int ag_target_info_is_valid(const ag_target_info_t *target);
const ag_data_layout_t *ag_target_info_data_layout(
    const ag_target_info_t *target);
int ag_data_layout_is_valid(const ag_data_layout_t *layout);
int ag_data_layout_equal(
    const ag_data_layout_t *lhs, const ag_data_layout_t *rhs);
int ag_data_layout_pointer_size(const ag_data_layout_t *layout);
int ag_data_layout_pointer_alignment(const ag_data_layout_t *layout);
int ag_data_layout_atomic_promoted_max_size(
    const ag_data_layout_t *layout);
int ag_data_layout_atomic_max_alignment(
    const ag_data_layout_t *layout);
int ag_data_layout_scalar_size(
    const ag_data_layout_t *layout, ag_target_scalar_kind_t kind);
int ag_data_layout_scalar_alignment(
    const ag_data_layout_t *layout, ag_target_scalar_kind_t kind);
ag_target_call_abi_t ag_target_info_call_abi(
    const ag_target_info_t *target);
int ag_target_info_equal(
    const ag_target_info_t *lhs, const ag_target_info_t *rhs);
#endif
