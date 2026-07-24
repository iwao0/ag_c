/* Pointer-sized integer static initializers retain symbol relocations. */
#include <assert.h>
#include <stdint.h>

static int object;
static int values[3];
static struct {
  int first;
  int second;
} record;

static int function(void) {
  return 3;
}

static uintptr_t object_address = (uintptr_t)&object;
static uintptr_t member_address = (uintptr_t)&record.second;
static uintptr_t function_address = (uintptr_t)function;
static uintptr_t element_address = (uintptr_t)&values[2];
static uintptr_t selected_address =
    (uintptr_t)(1 ? &values[1] : &values[0]);
static uintptr_t string_address = (uintptr_t)("hello" + 1);
static uintptr_t address_values[] = {
  (uintptr_t)&object,
  (uintptr_t)&record.first,
  (uintptr_t)function
};
static struct {
  uintptr_t object;
  uintptr_t function;
} address_record = {
  (uintptr_t)&record.second,
  (uintptr_t)function
};

int main(void) {
  static uintptr_t local_address = (uintptr_t)&values[0];

  assert(object_address == (uintptr_t)&object);
  assert(member_address == (uintptr_t)&record.second);
  assert(function_address == (uintptr_t)function);
  assert(element_address == (uintptr_t)&values[2]);
  assert(selected_address == (uintptr_t)&values[1]);
  assert(*(char *)string_address == 'e');
  assert(address_values[0] == (uintptr_t)&object);
  assert(address_values[1] == (uintptr_t)&record.first);
  assert(address_values[2] == (uintptr_t)function);
  assert(address_record.object == (uintptr_t)&record.second);
  assert(address_record.function == (uintptr_t)function);
  assert(local_address == (uintptr_t)&values[0]);
  return 0;
}
