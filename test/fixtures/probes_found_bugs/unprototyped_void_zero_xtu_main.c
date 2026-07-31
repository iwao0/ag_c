// A cross-TU () -> void call needs unspecified-parameter metadata even though
// its low-level Wasm signature is identical to an empty function slot.
// Expected with the companion TU: exit=42.

void record_zero_parameter_call();

extern int recorded_zero_parameter_calls;

static void (*taken_address)() = record_zero_parameter_call;

int main(void) {
  record_zero_parameter_call();
  taken_address();
  return recorded_zero_parameter_calls == 2 ? 42 : 1;
}
