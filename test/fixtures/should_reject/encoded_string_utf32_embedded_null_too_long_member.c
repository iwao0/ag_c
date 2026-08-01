/* A UTF-32 character plus an explicit NUL needs two member elements. */
#include <uchar.h>

struct value {
  char32_t text[1];
};

struct value instance = {U"\U0001F600\0"};

int main(void) {
  return 0;
}
