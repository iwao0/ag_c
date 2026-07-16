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
  AG_TARGET_CALL_ABI_AAPCS64,
  AG_TARGET_CALL_ABI_WASM32,
} ag_target_call_abi_t;

typedef struct ag_target_info_t {
  int pointer_size;
  int pointer_alignment;
  ag_target_call_abi_t call_abi;
  ag_target_scalar_layout_t scalar[AG_TARGET_SCALAR_COUNT];
} ag_target_info_t;

/* Explicit target description owned by a compilation session. */
ag_target_info_t ag_target_info_host(void);
ag_target_info_t ag_target_info_wasm32(void);
int ag_target_info_pointer_size(const ag_target_info_t *target);
int ag_target_info_pointer_alignment(const ag_target_info_t *target);
ag_target_call_abi_t ag_target_info_call_abi(
    const ag_target_info_t *target);
int ag_target_info_scalar_size(
    const ag_target_info_t *target, ag_target_scalar_kind_t kind);
int ag_target_info_scalar_alignment(
    const ag_target_info_t *target, ag_target_scalar_kind_t kind);
int ag_target_info_equal(
    const ag_target_info_t *lhs, const ag_target_info_t *rhs);
#endif
