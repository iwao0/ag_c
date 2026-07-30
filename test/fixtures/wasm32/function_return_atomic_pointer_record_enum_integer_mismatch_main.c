enum atomic_record_return_unsigned_enum {
  ATOMIC_RECORD_RETURN_ZERO = 0,
  ATOMIC_RECORD_RETURN_VALUE = 42
};

struct atomic_record_return_payload {
  enum atomic_record_return_unsigned_enum value;
};

_Atomic(struct atomic_record_return_payload *)
get_atomic_pointer_record_enum_integer(void);

int main(void) {
  _Atomic(struct atomic_record_return_payload *) value =
      get_atomic_pointer_record_enum_integer();
  return value->value;
}
