#include <assert.h>

#define CLASSIFY_SMALL_CONSUMER(expression) \
  _Generic((expression), \
           plain_small_consumer_function *: 1, \
           const_small_consumer_function *: 2, \
           volatile_small_consumer_function *: 3, \
           default: 0)

#define CLASSIFY_LARGE_CONSUMER(expression) \
  _Generic((expression), \
           plain_large_consumer_function *: 1, \
           const_large_consumer_function *: 2, \
           volatile_large_consumer_function *: 3, \
           default: 0)

struct small_pair {
  int left;
  int right;
};

struct large_packet {
  int values[6];
};

typedef struct small_pair plain_small_callback_function(void);
typedef const struct small_pair const_small_callback_function(void);
typedef volatile struct small_pair volatile_small_callback_function(void);
typedef struct large_packet plain_large_callback_function(void);
typedef const struct large_packet const_large_callback_function(void);
typedef volatile struct large_packet volatile_large_callback_function(void);

typedef int plain_small_consumer_function(
    plain_small_callback_function *callback);
typedef int const_small_consumer_function(
    const_small_callback_function *callback);
typedef int volatile_small_consumer_function(
    volatile_small_callback_function *callback);
typedef int plain_large_consumer_function(
    plain_large_callback_function *callback);
typedef int const_large_consumer_function(
    const_large_callback_function *callback);
typedef int volatile_large_consumer_function(
    volatile_large_callback_function *callback);

static struct small_pair plain_small_callback(void) {
  return (struct small_pair){19, 23};
}

static const struct small_pair const_small_callback(void) {
  return (struct small_pair){17, 25};
}

static volatile struct small_pair volatile_small_callback(void) {
  return (struct small_pair){13, 29};
}

static struct large_packet plain_large_callback(void) {
  return (struct large_packet){{1, 2, 3, 4, 5, 27}};
}

static const struct large_packet const_large_callback(void) {
  return (struct large_packet){{2, 3, 5, 7, 11, 14}};
}

static volatile struct large_packet volatile_large_callback(void) {
  return (struct large_packet){{6, 6, 6, 6, 6, 12}};
}

static int consume_plain_small(
    plain_small_callback_function *callback) {
  struct small_pair value = callback();
  return value.left + value.right;
}

static int consume_const_small(
    const_small_callback_function *callback) {
  struct small_pair value = callback();
  return value.left + value.right;
}

static int consume_volatile_small(
    volatile_small_callback_function *callback) {
  struct small_pair value = callback();
  return value.left + value.right;
}

static int sum_large(struct large_packet value) {
  int total = 0;
  for (int index = 0; index < 6; ++index) {
    total += value.values[index];
  }
  return total;
}

static int consume_plain_large(
    plain_large_callback_function *callback) {
  return sum_large(callback());
}

static int consume_const_large(
    const_large_callback_function *callback) {
  return sum_large(callback());
}

static int consume_volatile_large(
    volatile_large_callback_function *callback) {
  return sum_large(callback());
}

int main(void) {
  plain_small_consumer_function *plain_small_consumer =
      consume_plain_small;
  const_small_consumer_function *const_small_consumer =
      consume_const_small;
  volatile_small_consumer_function *volatile_small_consumer =
      consume_volatile_small;
  plain_large_consumer_function *plain_large_consumer =
      consume_plain_large;
  const_large_consumer_function *const_large_consumer =
      consume_const_large;
  volatile_large_consumer_function *volatile_large_consumer =
      consume_volatile_large;

  _Static_assert(
      CLASSIFY_SMALL_CONSUMER(&consume_plain_small) == 1,
      "plain small aggregate return remains unqualified");
  _Static_assert(
      CLASSIFY_SMALL_CONSUMER(&consume_const_small) == 2,
      "const small aggregate return remains distinct");
  _Static_assert(
      CLASSIFY_SMALL_CONSUMER(&consume_volatile_small) == 3,
      "volatile small aggregate return remains distinct");
  _Static_assert(
      CLASSIFY_LARGE_CONSUMER(&consume_plain_large) == 1,
      "plain large aggregate return remains unqualified");
  _Static_assert(
      CLASSIFY_LARGE_CONSUMER(&consume_const_large) == 2,
      "const large aggregate return remains distinct");
  _Static_assert(
      CLASSIFY_LARGE_CONSUMER(&consume_volatile_large) == 3,
      "volatile large aggregate return remains distinct");

  assert(plain_small_consumer(plain_small_callback) == 42);
  assert(const_small_consumer(const_small_callback) == 42);
  assert(volatile_small_consumer(volatile_small_callback) == 42);
  assert(plain_large_consumer(plain_large_callback) == 42);
  assert(const_large_consumer(const_large_callback) == 42);
  assert(volatile_large_consumer(volatile_large_callback) == 42);
  return 0;
}
