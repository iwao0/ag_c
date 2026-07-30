extern _Atomic(int) global_atomic_qualifier_value;

int main(void) {
  return global_atomic_qualifier_value;
}
