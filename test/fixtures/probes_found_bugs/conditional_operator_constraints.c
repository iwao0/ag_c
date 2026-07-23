#include <assert.h>
#include <stdatomic.h>

#define IS_TYPE(expression, type) \
  _Generic((expression), type: 1, default: 0)

struct record_value {
  int member;
};

struct incomplete_value;
typedef int incomplete_array[];

static int complete_array[3] = {4, 5, 6};

static int add_one(int value) {
  return value + 1;
}

static int subtract_one(int value) {
  return value - 1;
}

int old_style_function();
int prototype_function(int);

static void set_value(int *target, int value) {
  *target = value;
}

int main(void) {
  int choose_left = 1;
  int first = 7;
  int second = 9;
  const int *const_pointer = &first;
  volatile int *volatile_pointer = &second;
  void *void_pointer = &first;
  const volatile int *qualified_result =
      choose_left ? const_pointer : volatile_pointer;

  _Static_assert(
      IS_TYPE(choose_left ? const_pointer : volatile_pointer,
              const volatile int *),
      "conditional pointer result retains both referenced qualifiers");
  _Static_assert(
      IS_TYPE(choose_left ? void_pointer : const_pointer,
              const void *),
      "object and void pointer produce qualified void pointer");
  _Static_assert(
      IS_TYPE(choose_left ? const_pointer : (4 - 4),
              const int *),
      "integer constant expression zero is a null pointer constant");
  _Static_assert(
      IS_TYPE(choose_left ? add_one : subtract_one,
              int (*)(int)),
      "compatible function designators produce a function pointer");
  _Static_assert(
      IS_TYPE(choose_left ? old_style_function : prototype_function,
              int (*)(int)),
      "compatible function types produce their prototype composite type");

  incomplete_array *unknown_bound = (incomplete_array *)0;
  int (*known_bound)[3] = &complete_array;
  _Static_assert(
      IS_TYPE(choose_left ? unknown_bound : known_bound,
      int (*)[3]),
      "unknown and known array bounds produce the known composite bound");
  incomplete_array **unknown_handle = &unknown_bound;
  int (**known_handle)[3] = &known_bound;
  _Static_assert(
      sizeof **(choose_left ? unknown_handle : known_handle) ==
          sizeof complete_array,
      "nested compatible pointers recursively retain the known bound");

  struct incomplete_value *incomplete_pointer = 0;
  const void *const_void_pointer = 0;
  _Static_assert(
      IS_TYPE(choose_left ? incomplete_pointer : const_void_pointer,
              const void *),
      "incomplete object and void pointer produce qualified void pointer");

  _Atomic int atomic_value = 11;
  _Atomic int *atomic_pointer = &atomic_value;
  _Static_assert(
      IS_TYPE(choose_left ? atomic_pointer : void_pointer, void *),
      "atomic object pointer converts to unqualified void pointer");

  assert(*qualified_result == 7);
  choose_left = 0;
  qualified_result =
      choose_left ? const_pointer : volatile_pointer;
  assert(*qualified_result == 9);
  assert((choose_left ? add_one : subtract_one)(10) == 9);

  struct record_value left_record = {13};
  struct record_value right_record = {17};
  struct record_value selected_record =
      choose_left ? left_record : right_record;
  assert(selected_record.member == 17);
  assert((choose_left ? 1.5 : 2) == 2.0);

  int side_effect = 0;
  (choose_left ? set_value(&side_effect, 1)
               : set_value(&side_effect, 2));
  assert(side_effect == 2);
  return 0;
}
