extern _Atomic(int *)
    global_multidimensional_array_atomic_pointer[1][1];

int main(void) {
  return global_multidimensional_array_atomic_pointer[0][0] != 0;
}
