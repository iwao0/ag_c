/* Exact bounds may omit only the string literal's implicit terminator. */
#include <assert.h>

struct Holder {
  char text[3];
};

char global_direct[3] = "abc";
char global_braced[3] = {"def"};
struct Holder global_holder = {.text = "ghi"};
char global_rows[1][3] = {"jkl"};

int main(void) {
  char local_direct[3] = "mno";
  char local_braced[3] = {"pqr"};
  struct Holder local_holder = {.text = "stu"};
  char local_rows[1][3] = {"vwx"};

  assert(sizeof(global_direct) == 3);
  assert(global_direct[2] == 'c');
  assert(global_braced[2] == 'f');
  assert(global_holder.text[2] == 'i');
  assert(global_rows[0][2] == 'l');
  assert(sizeof(local_direct) == 3);
  assert(local_direct[2] == 'o');
  assert(local_braced[2] == 'r');
  assert(local_holder.text[2] == 'u');
  assert(local_rows[0][2] == 'x');
  assert(((char[3]){"yz!"})[2] == '!');
  assert(((struct Holder){.text = "ABC"}).text[2] == 'C');
  return 0;
}
