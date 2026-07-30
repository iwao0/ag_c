extern _Atomic(int *) atomic_pointer_slots[2];
extern _Atomic(int) *atomic_pointee_slots[2];

int main(void) {
  return *atomic_pointer_slots[0] + *atomic_pointee_slots[1];
}
