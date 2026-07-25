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
  return 0;
}
