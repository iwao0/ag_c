_Atomic(int *) make_atomic_pointer(void);
_Atomic(int) *make_atomic_pointee_pointer(void);

int main(void) {
  _Atomic(int *) pointer = make_atomic_pointer();
  return *pointer + *make_atomic_pointee_pointer();
}
