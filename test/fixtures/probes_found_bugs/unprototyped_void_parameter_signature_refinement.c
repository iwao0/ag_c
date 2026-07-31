// A function address may establish an unspecified-parameter placeholder before
// direct and indirect calls refine a nonempty void-returning Wasm signature.
// Expected: exit=0.

struct pair {
  int left;
  int right;
};

static void record_values();

static void (*taken_address)() = record_values;
static int call_count;
static int observed_total;
static int selector_count;

static void (*select_callback(void))() {
  selector_count++;
  return taken_address;
}

int main(void) {
  signed char first_integer = -3;
  float first_floating = 2.5f;
  struct pair first_pair = {20, 24};
  struct pair second_pair = {10, 21};

  record_values(first_integer, first_floating, first_pair);
  select_callback()(7, 1.5f, second_pair);

  if (call_count != 2)
    return 1;
  if (observed_total != 87)
    return 2;
  if (selector_count != 1 || taken_address == 0)
    return 3;
  return 0;
}

static void record_values(
    int integer_value, double floating_value, struct pair pair_value) {
  call_count++;
  observed_total += integer_value + (int)(floating_value * 2.0) +
                    pair_value.left + pair_value.right;
}
