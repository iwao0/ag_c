#include <assert.h>

int typedef scalar_t;

struct pair {
  int first;
  int second;
} typedef pair_t;

scalar_t static post_typedef_static = 9;

struct box {
  int value;
};

struct box static post_tag_static = {4};

static int inherited_internal_object = 2;
extern int inherited_internal_object;

static int inherited_internal_function(void);
extern int inherited_internal_function(void);
int inherited_internal_function(void) {
  extern int inherited_internal_object;
  return inherited_internal_object;
}

_Thread_local extern int shared_tls;
int _Thread_local static private_tls = 6;
extern _Thread_local int private_tls;
_Thread_local int shared_tls = 8;

static int read_late_tls(void) {
  extern _Thread_local int late_tls;
  return late_tls;
}

_Thread_local int late_tls = 3;

static int read_values(int register parameter) {
  int auto automatic = parameter;
  scalar_t register copy = automatic + 1;
  int typedef local_function_type(void);
  static _Thread_local int local_tls = 5;
  extern _Thread_local int shared_tls;
  pair_t local_pair = {copy, local_tls};
  local_function_type *unused_function = 0;

  assert(unused_function == 0);
  return local_pair.first + local_pair.second + shared_tls;
}

int main(void) {
  assert(post_typedef_static == 9);
  assert(post_tag_static.value == 4);
  assert(inherited_internal_function() == 2);
  assert(private_tls == 6);
  assert(shared_tls == 8);
  assert(read_late_tls() == 3);
  assert(read_values(28) == 42);
  return 0;
}
