// Paired with unprototyped_atomic_funcptr_xtu_main.c.

int atomic_char_target(_Atomic(signed char) value) {
  return value != 0;
}

int atomic_float_target(_Atomic(float) value) {
  return value != 0.0f;
}
