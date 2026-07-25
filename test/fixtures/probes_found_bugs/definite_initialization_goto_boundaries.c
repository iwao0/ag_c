#include <assert.h>

static int forward_goto_paths_initialize(int condition) {
  int value;
  if (condition)
    goto alternate;
  value = 10;
  goto done;
alternate:
  value = 20;
done:
  return value;
}

static int multiple_forward_gotos_initialize(int selector) {
  int value;
  if (selector == 0)
    goto zero;
  if (selector == 1)
    goto one;
  value = 30;
  goto done;
zero:
  value = 40;
  goto done;
one:
  value = 50;
done:
  return value;
}

static int switch_goto_paths_initialize(int selector) {
  int value;
  switch (selector) {
    case 0:
      value = 60;
      goto done;
    default:
      value = 70;
      break;
  }
done:
  return value;
}

static int backward_goto_paths_initialize(int condition) {
  int value;
  goto initialize;
done:
  return value;
initialize:
  value = condition ? 80 : 90;
  goto done;
}

static int backward_goto_multiple_paths_initialize(int condition) {
  int value;
  goto dispatch;
done:
  return value;
dispatch:
  if (condition) {
    value = 100;
    goto done;
  }
  value = 110;
  goto done;
}

static int backward_goto_cycle_preserves_initialization(
    int iterations) {
  int value;
  goto initialize;
again:
  if (iterations-- > 0) {
    value += 1;
    goto again;
  }
  return value;
initialize:
  value = 120;
  goto again;
}

static void set_value(int *output, int value) {
  *output = value;
}

static int backward_goto_out_parameter_initializes(
    int condition) {
  int value;
  goto dispatch;
done:
  return value;
dispatch:
  set_value(&value, condition ? 130 : 140);
  goto done;
}

static int constant_unreachable_goto_is_ignored(void) {
  int value;
  if (0)
    goto done;
  value = 150;
done:
  return value;
}

int main(void) {
  assert(forward_goto_paths_initialize(0) == 10);
  assert(forward_goto_paths_initialize(1) == 20);
  assert(multiple_forward_gotos_initialize(0) == 40);
  assert(multiple_forward_gotos_initialize(1) == 50);
  assert(multiple_forward_gotos_initialize(2) == 30);
  assert(switch_goto_paths_initialize(0) == 60);
  assert(switch_goto_paths_initialize(1) == 70);
  assert(backward_goto_paths_initialize(0) == 90);
  assert(backward_goto_paths_initialize(1) == 80);
  assert(backward_goto_multiple_paths_initialize(0) == 110);
  assert(backward_goto_multiple_paths_initialize(1) == 100);
  assert(backward_goto_cycle_preserves_initialization(0) == 120);
  assert(backward_goto_cycle_preserves_initialization(3) == 123);
  assert(backward_goto_out_parameter_initializes(0) == 140);
  assert(backward_goto_out_parameter_initializes(1) == 130);
  assert(constant_unreachable_goto_is_ignored() == 150);
  return 0;
}
