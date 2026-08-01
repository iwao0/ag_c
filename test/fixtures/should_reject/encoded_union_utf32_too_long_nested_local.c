/* A nested union member cannot truncate a UTF-32 string's code units. */
#include <uchar.h>

union value {
  unsigned long long raw;
  char32_t text[1];
};

struct envelope {
  int prefix;
  union value value;
  int suffix;
};

int main(void) {
  struct envelope instance = {
      .prefix = 1,
      .value.text = U"ab",
      .suffix = 2,
  };
  return instance.prefix;
}
