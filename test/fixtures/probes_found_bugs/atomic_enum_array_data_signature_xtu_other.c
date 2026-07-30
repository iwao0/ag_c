// Paired with atomic_enum_array_data_signature_xtu_main.c.

static unsigned int atomic_enum_plain_row[2] = {19U, 0U};
static _Atomic(unsigned int)
    atomic_enum_element_row[2] = {0U, 23U};

_Atomic(unsigned int (*)[2])
    shared_atomic_enum_row_pointer =
        &atomic_enum_plain_row;
_Atomic(unsigned int)
    (*shared_atomic_enum_element_row)[2] =
        &atomic_enum_element_row;
