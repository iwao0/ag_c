/* An array designator does not relax a selected union member's string bound. */
#include <wchar.h>

union value {
  unsigned long long raw;
  wchar_t text[1];
};

union value instances[2] = {[1].text = L"ab"};

int main(void) {
  return 0;
}
