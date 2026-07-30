// Paired with atomic_enum_anonymous_union_data_signature_xtu_main.c.

#ifndef AG_C_ATOMIC_ENUM_ANONYMOUS_UNION_DATA_SIGNATURE_XTU_TYPES
#define AG_C_ATOMIC_ENUM_ANONYMOUS_UNION_DATA_SIGNATURE_XTU_TYPES
typedef union {
  unsigned int bits;
  unsigned int value;
} atomic_enum_anonymous_union_t;
#endif

static atomic_enum_anonymous_union_t plain_payload = {
    .value = 19U};
static _Atomic(atomic_enum_anonymous_union_t) atomic_payload =
    (atomic_enum_anonymous_union_t){.value = 23U};

_Atomic(atomic_enum_anonymous_union_t *)
    shared_atomic_anonymous_union_pointer = &plain_payload;
_Atomic(atomic_enum_anonymous_union_t) *
    shared_atomic_anonymous_union_pointee = &atomic_payload;
