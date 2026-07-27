// Definition TU for the cross-TU C11 thread-local fixture.
#include "test/fixtures/probes_found_bugs/thread_local_xtu_boundaries.h"

_Alignas(16) _Thread_local struct thread_local_payload
    shared_thread_payload = {3, 4.5};
_Thread_local int shared_thread_value = 7;
static _Thread_local int private_thread_value = 23;

int other_read_shared_thread_value(void) {
  return shared_thread_value;
}

int other_add_shared_thread_value(int value) {
  return shared_thread_value += value;
}

int *other_shared_thread_address(void) {
  return &shared_thread_value;
}

int *other_private_thread_address(void) {
  return &private_thread_value;
}

struct thread_local_payload *other_thread_payload_address(void) {
  return &shared_thread_payload;
}
