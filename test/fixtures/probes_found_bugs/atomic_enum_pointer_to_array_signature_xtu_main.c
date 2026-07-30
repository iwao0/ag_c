// Enum compatibility is preserved inside an atomic pointer-to-array and a
// pointer to an array of atomic enum elements.
// Expected with the companion TU: exit=42.

enum atomic_enum_signed_row_value {
  ATOMIC_ENUM_SIGNED_ROW_NEGATIVE = -1,
  ATOMIC_ENUM_SIGNED_ROW_VALUE = 20
};

enum atomic_enum_unsigned_row_value {
  ATOMIC_ENUM_UNSIGNED_ROW_ZERO = 0,
  ATOMIC_ENUM_UNSIGNED_ROW_VALUE = 22
};

_Atomic(enum atomic_enum_signed_row_value (*)[2])
roundtrip_atomic_enum_row_pointer(
    _Atomic(enum atomic_enum_signed_row_value (*)[2]) row);

int read_atomic_enum_element_row(
    _Atomic(enum atomic_enum_unsigned_row_value) (*row)[2]);

int main(void) {
  enum atomic_enum_signed_row_value signed_row[2] = {
      ATOMIC_ENUM_SIGNED_ROW_NEGATIVE,
      ATOMIC_ENUM_SIGNED_ROW_VALUE};
  _Atomic(enum atomic_enum_unsigned_row_value)
      unsigned_row[2] = {
          ATOMIC_ENUM_UNSIGNED_ROW_ZERO,
          ATOMIC_ENUM_UNSIGNED_ROW_VALUE};
  _Atomic(enum atomic_enum_signed_row_value (*)[2]) pointer =
      &signed_row;
  _Atomic(enum atomic_enum_signed_row_value (*)[2]) result =
      roundtrip_atomic_enum_row_pointer(pointer);
  return (*result)[1] +
         read_atomic_enum_element_row(&unsigned_row);
}
