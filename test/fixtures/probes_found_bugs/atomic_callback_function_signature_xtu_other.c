typedef _Atomic(int) atomic_callback_t(_Atomic(int) value);

_Atomic(int) invoke_atomic_callback(
    atomic_callback_t *callback, _Atomic(int) value) {
  return callback(value);
}
