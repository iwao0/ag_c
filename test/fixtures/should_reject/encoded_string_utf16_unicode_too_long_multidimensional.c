/* Each multidimensional row must hold every UTF-16 code unit. */
#include <uchar.h>

char16_t rows[1][1] = {u"\U0001F600"};

int main(void) {
  return 0;
}
