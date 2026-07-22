#include <assert.h>

typedef int incomplete_array[];

static int read_const_array(const int (*pointer)[3]) {
  return (*pointer)[1];
}

static const int (*as_const_array(int (*pointer)[3]))[3] {
  return pointer;
}

int main(void) {
  int values[3] = {3, 5, 7};
  int (*plain_pointer)[3] = &values;
  const int (*const_pointer)[3] = plain_pointer;

  assert(read_const_array(plain_pointer) == 5);
  assert((*as_const_array(plain_pointer))[2] == 7);
  assert(plain_pointer == const_pointer);
  assert(!(plain_pointer < const_pointer));
  assert(!(const_pointer < plain_pointer));
  assert(plain_pointer - const_pointer == 0);

  incomplete_array *unknown_bound = plain_pointer;
  int (*known_bound)[3] = unknown_bound;
  assert((*known_bound)[0] == 3);

  const int (*explicit_const_pointer)[3] =
      (const int (*)[3])plain_pointer;
  assert((*explicit_const_pointer)[2] == 7);
  return 0;
}
