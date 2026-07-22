#include <assert.h>

int main(void) {
  int (*selected_row)[3] =
      &((int[2][3]){{1, 2, 3}, {4, 5, 6}})[1];

  assert((*selected_row)[0] == 4);
  assert((*selected_row)[2] == 6);
  (*selected_row)[1] = 50;
  assert((*selected_row)[1] == 50);

  assert(_Generic(&((int[2][3]){{7, 8, 9}, {10, 11, 12}})[0],
                  int (*)[3]: 1,
                  default: 0));
  return 0;
}
