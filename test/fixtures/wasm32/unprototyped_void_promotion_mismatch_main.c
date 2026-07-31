// The call fixes a double parameter ABI, which is incompatible with the float
// prototype in the companion TU even though the function returns void.

void record_float_value();

static void (*taken_address)() = record_float_value;

int main(void) {
  record_float_value(2.5f);
  return taken_address != 0 ? 0 : 1;
}
