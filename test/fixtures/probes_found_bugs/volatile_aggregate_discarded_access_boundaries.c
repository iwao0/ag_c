/*
 * Converting a volatile struct or union lvalue to a discarded value still
 * reads the complete object.  Keeping only the source address is not an
 * implementation of the volatile access.
 */
#include <assert.h>

struct pair {
  int left;
  int right;
};

struct packet {
  long values[5];
};

union choice {
  unsigned long long bits;
  double real;
};

struct aggregate_member {
  volatile struct pair member;
  int tail;
};

static volatile struct pair global_pairs[2] = {
    {3, 5},
    {7, 11},
};
static volatile struct packet global_packet = {
    {13, 17, 19, 23, 29},
};
static volatile union choice global_choice = {
    .bits = 0x1122334455667788ULL,
};
static volatile struct pair * volatile active_pointer =
    &global_pairs[0];
static int selector_calls;

static volatile struct pair *select_pair(
    volatile struct pair *pointer) {
  selector_calls++;
  return pointer;
}

static int pair_is(
    const volatile struct pair *value, int left, int right) {
  return value->left == left && value->right == right;
}

int main(void) {
  volatile struct pair local = {31, 37};
  volatile struct pair *pointer = &local;
  struct aggregate_member container = {
      {41, 43},
      47,
  };

  (void)global_pairs[1];
  (void)local;
  (void)*pointer;
  (void)*active_pointer;
  (void)container.member;
  (void)global_packet;
  (void)global_choice;
  (void)(volatile struct pair){53, 59};

  selector_calls = 0;
  (void)*select_pair(&global_pairs[0]);
  assert(selector_calls == 1);

  selector_calls = 0;
  (void)(selector_calls++, global_pairs[0]);
  assert(selector_calls == 1);

  /*
   * The member-designator path needs the original lvalue address, not the
   * materialized value temporary used by a whole-object conversion.
   */
  container.member.left = 61;

  assert(pair_is(&global_pairs[0], 3, 5));
  assert(pair_is(&global_pairs[1], 7, 11));
  assert(pair_is(&local, 31, 37));
  assert(pair_is(pointer, 31, 37));
  assert(active_pointer == &global_pairs[0]);
  assert(pair_is(&container.member, 61, 43));
  assert(container.tail == 47);
  assert(global_packet.values[0] == 13);
  assert(global_packet.values[4] == 29);
  assert(global_choice.bits == 0x1122334455667788ULL);
  return 0;
}
