#include <assert.h>

struct variant_record {
  unsigned char head;
  unsigned int word;
  unsigned char tail;
};

union variant_value {
  struct variant_record record;
  double number;
  unsigned char raw[32];
};

struct variant_envelope {
  unsigned char prefix;
  union variant_value values[3];
  unsigned char suffix;
};

static union variant_value global_values[3] = {
    {.record = {1, 0x12345678u, 2}},
    {.number = 3.5},
    {.record = {4, 0x23456789u, 5}},
};

static struct variant_envelope global_envelope = {
    .prefix = 6,
    .values = {
        {.record = {7, 0x3456789au, 8}},
        {.number = 4.5},
        {.record = {9, 0x456789abu, 10}},
    },
    .suffix = 11,
};

static int values_match(const union variant_value *values,
                        unsigned char first_head, unsigned int first_word,
                        unsigned char first_tail, double number,
                        unsigned char last_head, unsigned int last_word,
                        unsigned char last_tail) {
  return values[0].record.head == first_head &&
         values[0].record.word == first_word &&
         values[0].record.tail == first_tail &&
         values[1].number == number &&
         values[2].record.head == last_head &&
         values[2].record.word == last_word &&
         values[2].record.tail == last_tail;
}

static const union variant_value *static_values(void) {
  static union variant_value values[3] = {
      {.record = {12, 0x56789abcu, 13}},
      {.number = 5.5},
      {.record = {14, 0x6789abcdu, 15}},
  };
  return values;
}

int main(void) {
  union variant_value local_values[3] = {
      {.record = {16, 0x789abcdeu, 17}},
      {.number = 6.5},
      {.record = {18, 0x89abcdefu, 19}},
  };
  assert(values_match(global_values, 1, 0x12345678u, 2, 3.5,
                      4, 0x23456789u, 5));
  assert(values_match(local_values, 16, 0x789abcdeu, 17, 6.5,
                      18, 0x89abcdefu, 19));
  assert(values_match(static_values(), 12, 0x56789abcu, 13, 5.5,
                      14, 0x6789abcdu, 15));
  assert(global_envelope.prefix == 6);
  assert(values_match(global_envelope.values, 7, 0x3456789au, 8, 4.5,
                      9, 0x456789abu, 10));
  assert(global_envelope.suffix == 11);
  return 0;
}
