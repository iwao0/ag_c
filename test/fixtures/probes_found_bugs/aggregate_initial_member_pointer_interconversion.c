/*
 * A suitably converted pointer to a structure points to its initial member,
 * and vice versa.  A union pointer has the same relation with every member.
 */
#include <assert.h>

struct record {
  int key;
  double value;
};

struct array_record {
  int values[3];
  int tail;
};

union overlay {
  long integer;
  double floating;
  unsigned char bytes[8];
};

static struct record global_record = {17, 2.5};

static struct record *record_from_key(int *key) {
  return (struct record *)key;
}

int main(void) {
  struct record local_record = {23, 4.5};
  struct array_record array_record = {{3, 5, 7}, 11};
  union overlay overlay = {.integer = 1234567L};

  int *global_key = (int *)&global_record;
  int *local_key = (int *)&local_record;
  assert(global_key == &global_record.key);
  assert(local_key == &local_record.key);
  assert(record_from_key(global_key) == &global_record);
  assert(record_from_key(local_key) == &local_record);
  assert(record_from_key(global_key)->value == 2.5);
  assert(record_from_key(local_key)->value == 4.5);

  int (*values)[3] = (int (*)[3])&array_record;
  assert(values == &array_record.values);
  assert(((struct array_record *)values)->tail == 11);
  assert((*values)[0] == 3);
  assert((*values)[2] == 7);

  assert((void *)&overlay == (void *)&overlay.integer);
  assert((void *)&overlay == (void *)&overlay.floating);
  assert((void *)&overlay == (void *)&overlay.bytes);
  assert(((union overlay *)&overlay.integer)->integer == 1234567L);
  assert(((union overlay *)&overlay.floating)->integer == 1234567L);
  assert(((union overlay *)&overlay.bytes)->integer == 1234567L);
  return 0;
}
