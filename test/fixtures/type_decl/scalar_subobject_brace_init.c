/* A scalar subobject may have its own single brace-enclosed initializer. */
#include <assert.h>

struct Item {
  int value;
};

struct NestedItems {
  struct Item values[1];
};

struct Item global_item = {{1}};
int global_values[1] = {{2}};
struct NestedItems global_nested = {{{9}}};

int main(void) {
  struct Item local_item = {{3}};
  int local_values[1] = {{4}};
  static struct Item static_item = {{5}};
  static int static_values[1] = {{6}};
  struct NestedItems local_nested = {{{10}}};
  static struct NestedItems static_nested = {{{11}}};

  assert(global_item.value == 1);
  assert(global_values[0] == 2);
  assert(local_item.value == 3);
  assert(local_values[0] == 4);
  assert(static_item.value == 5);
  assert(static_values[0] == 6);
  assert(((struct Item){{7}}).value == 7);
  assert((int[1]){{8}}[0] == 8);
  assert(global_nested.values[0].value == 9);
  assert(local_nested.values[0].value == 10);
  assert(static_nested.values[0].value == 11);
  assert(((struct NestedItems){{{12}}}).values[0].value == 12);
  return 0;
}
