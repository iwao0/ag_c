#include <stdatomic.h>

enum State {
  STATE_READY,
  STATE_DONE
};

// The enum's compatible integer representation does not remove object constness.
int main(void) {
  const _Atomic(enum State) state = STATE_READY;
  return atomic_exchange(&state, STATE_DONE);
}
