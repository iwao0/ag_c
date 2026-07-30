typedef _Atomic(int) atomic_callback_t(
    const volatile _Atomic(int) value);

_Atomic(int) invoke_atomic_callback(
    atomic_callback_t *callback, _Atomic(int) value);

static _Atomic(int) add_two(_Atomic(int) value) {
  return value + 2;
}

int main(void) {
  _Atomic(int) value = 40;
  _Atomic(int) result = invoke_atomic_callback(add_two, value);
  return result;
}
