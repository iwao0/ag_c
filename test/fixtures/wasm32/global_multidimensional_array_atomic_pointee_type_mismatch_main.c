extern _Atomic(int) *
    global_multidimensional_array_atomic_pointee[1][1];

int main(void) {
  return *global_multidimensional_array_atomic_pointee[0][0];
}
