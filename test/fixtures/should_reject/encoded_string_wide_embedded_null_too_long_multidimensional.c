/* A wide character plus an explicit NUL cannot fit in a one-element row. */
#include <wchar.h>

wchar_t rows[1][1] = {L"\U0001F600\0"};

int main(void) {
  return 0;
}
