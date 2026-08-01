/* A global multidimensional member of signed int is incompatible with UTF-32. */
struct rows {
  int values[1][3];
};

struct rows value = {.values = {U"hi"}};

int main(void) {
  return 0;
}
