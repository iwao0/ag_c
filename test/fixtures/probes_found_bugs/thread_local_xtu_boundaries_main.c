// Cross-TU C11 thread-local linkage and identity boundaries.
// Expected with thread_local_xtu_boundaries_other.c: exit=42.
#include "test/fixtures/probes_found_bugs/thread_local_xtu_boundaries.h"

static _Thread_local int private_thread_value = 11;

static int block_read_shared_thread_value(void) {
  extern _Thread_local int shared_thread_value;
  return shared_thread_value;
}

static unsigned long pointer_mod(void *pointer, unsigned long alignment) {
  return (unsigned long)pointer % alignment;
}

int main(void) {
  if (shared_thread_value != 7 ||
      other_read_shared_thread_value() != 7 ||
      block_read_shared_thread_value() != 7) {
    return 1;
  }
  if (other_shared_thread_address() != &shared_thread_value) return 2;
  if (other_private_thread_address() == &private_thread_value) return 3;
  if (*other_private_thread_address() != 23 ||
      private_thread_value != 11) {
    return 4;
  }
  if (other_thread_payload_address() != &shared_thread_payload) return 5;
  if (pointer_mod(&shared_thread_payload, 16) != 0) return 6;
  if (shared_thread_payload.count != 3 ||
      shared_thread_payload.value != 4.5) {
    return 7;
  }

  shared_thread_value += 5;
  if (other_read_shared_thread_value() != 12) return 8;
  if (other_add_shared_thread_value(8) != 20 ||
      shared_thread_value != 20) {
    return 9;
  }

  shared_thread_payload.count += 4;
  shared_thread_payload.value = 9.25;
  if (other_thread_payload_address()->count != 7 ||
      other_thread_payload_address()->value != 9.25) {
    return 10;
  }
  return 42;
}
