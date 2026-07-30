// Discarded scalar atomic lvalue-conversion boundaries.
// Expected: exit=0

typedef int callback_t(int);

struct holder {
  _Atomic unsigned short number;
  _Atomic double real;
  _Atomic(int *) pointer;
};

static int values[4] = {3, 5, 7, 11};

static int add_one(int value) {
  return value + 1;
}

static _Atomic unsigned char global_byte = 13;
static _Atomic unsigned short global_short = 50000;
static _Atomic int global_int = -17;
static _Atomic unsigned long long global_wide =
    0xfedcba9876543210ULL;
static _Atomic float global_float = 19.0f;
static _Atomic double global_double = -23.0;
static _Atomic(int *) global_pointer = values + 1;
static _Atomic(callback_t *) global_callback = add_one;
static _Atomic int int_slots[2] = {29, 31};
static struct holder global_holder = {
    32000, 37.0, values + 2};

static int int_selector_evaluations;
static int float_selector_evaluations;
static int pointer_selector_evaluations;
static int callback_selector_evaluations;

static _Atomic int *select_int(void) {
  int_selector_evaluations++;
  return &int_slots[1];
}

static _Atomic float *select_float(void) {
  float_selector_evaluations++;
  return &global_float;
}

static _Atomic(int *) *select_pointer(void) {
  pointer_selector_evaluations++;
  return &global_pointer;
}

static _Atomic(callback_t *) *select_callback(void) {
  callback_selector_evaluations++;
  return &global_callback;
}

int main(void) {
  _Atomic unsigned char local_byte = 41;
  _Atomic unsigned short local_short = 42000;
  _Atomic int local_int = -43;
  _Atomic unsigned long long local_wide =
      0x1122334455667788ULL;
  _Atomic float local_float = -47.0f;
  _Atomic double local_double = 53.0;
  _Atomic(int *) local_pointer = values + 3;
  _Atomic(callback_t *) local_callback = add_one;

  _Atomic unsigned char *byte_pointer = &local_byte;
  _Atomic unsigned short *short_pointer = &local_short;
  _Atomic int *int_pointer = &local_int;
  _Atomic unsigned long long *wide_pointer = &local_wide;
  _Atomic float *float_pointer = &local_float;
  _Atomic double *double_pointer = &local_double;
  _Atomic(int *) *pointer_pointer = &local_pointer;
  _Atomic(callback_t *) *callback_pointer = &local_callback;
  _Atomic int * volatile volatile_pointer = &global_int;

  (void)global_byte;
  (void)global_short;
  (void)global_int;
  (void)global_wide;
  (void)global_float;
  (void)global_double;
  (void)global_pointer;
  (void)global_callback;

  (void)local_byte;
  (void)local_short;
  (void)local_int;
  (void)local_wide;
  (void)local_float;
  (void)local_double;
  (void)local_pointer;
  (void)local_callback;

  (void)*byte_pointer;
  (void)*short_pointer;
  (void)*int_pointer;
  (void)*wide_pointer;
  (void)*float_pointer;
  (void)*double_pointer;
  (void)*pointer_pointer;
  (void)*callback_pointer;
  (void)*volatile_pointer;

  (void)int_slots[0];
  (void)global_holder.number;
  (void)global_holder.real;
  (void)global_holder.pointer;

  (void)*select_int();
  (void)*select_float();
  (void)*select_pointer();
  (void)*select_callback();

  if (int_selector_evaluations != 1 ||
      float_selector_evaluations != 1 ||
      pointer_selector_evaluations != 1 ||
      callback_selector_evaluations != 1)
    return 1;

  if (global_byte != 13 || global_short != 50000 ||
      global_int != -17 ||
      global_wide != 0xfedcba9876543210ULL ||
      global_float != 19.0f || global_double != -23.0 ||
      global_pointer != values + 1 ||
      global_callback(58) != 59)
    return 2;
  if (local_byte != 41 || local_short != 42000 ||
      local_int != -43 ||
      local_wide != 0x1122334455667788ULL ||
      local_float != -47.0f || local_double != 53.0 ||
      local_pointer != values + 3 ||
      local_callback(60) != 61)
    return 3;
  if (int_slots[0] != 29 || int_slots[1] != 31 ||
      global_holder.number != 32000 ||
      global_holder.real != 37.0 ||
      global_holder.pointer != values + 2)
    return 4;
  return 0;
}
