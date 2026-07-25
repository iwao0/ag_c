#include <assert.h>

static int all_switch_paths_initialize(int selector) {
  int value;
  switch (selector) {
    case 0:
      value = 10;
      break;
    case 1:
      value = 20;
      break;
    default:
      value = 30;
      break;
  }
  return value;
}

static int branch_breaks_initialize(int selector, int condition) {
  int value;
  switch (selector) {
    case 0:
      if (condition) {
        value = 40;
        break;
      }
      value = 50;
      break;
    default:
      value = 60;
      break;
  }
  return value;
}

static int fallthrough_reinitializes(int selector) {
  int value;
  switch (selector) {
    case 0:
      value = 70;
      /* fall through */
    case 1:
      value = 80;
      break;
    default:
      value = 90;
      break;
  }
  return value;
}

static int nested_switches_initialize(int outer, int inner) {
  int value;
  switch (outer) {
    case 0:
      switch (inner) {
        case 0:
          value = 100;
          break;
        default:
          value = 110;
          break;
      }
      break;
    default:
      value = 120;
      break;
  }
  return value;
}

static int nested_loop_break_initializes(int selector) {
  int value;
  switch (selector) {
    case 0:
      while (1) {
        value = 130;
        break;
      }
      break;
    default:
      value = 140;
      break;
  }
  return value;
}

static int constant_switch_selects_initializing_case(void) {
  int value;
  switch (1) {
    case 1:
      value = 145;
      break;
    default:
      break;
  }
  return value;
}

static int switch_continue_initializes(int selector) {
  int value;
  for (int i = 0; i < 2; i++) {
    switch (selector + i) {
      case 0:
        value = 150;
        continue;
      default:
        value = 160;
        break;
    }
  }
  return value;
}

int main(void) {
  assert(all_switch_paths_initialize(0) == 10);
  assert(all_switch_paths_initialize(1) == 20);
  assert(all_switch_paths_initialize(9) == 30);
  assert(branch_breaks_initialize(0, 1) == 40);
  assert(branch_breaks_initialize(0, 0) == 50);
  assert(branch_breaks_initialize(9, 0) == 60);
  assert(fallthrough_reinitializes(0) == 80);
  assert(fallthrough_reinitializes(1) == 80);
  assert(fallthrough_reinitializes(9) == 90);
  assert(nested_switches_initialize(0, 0) == 100);
  assert(nested_switches_initialize(0, 9) == 110);
  assert(nested_switches_initialize(9, 0) == 120);
  assert(nested_loop_break_initializes(0) == 130);
  assert(nested_loop_break_initializes(9) == 140);
  assert(constant_switch_selects_initializing_case() == 145);
  assert(switch_continue_initializes(0) == 160);
  assert(switch_continue_initializes(1) == 160);
  return 0;
}
