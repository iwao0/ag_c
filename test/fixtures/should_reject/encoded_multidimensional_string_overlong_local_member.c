/* A local multidimensional member row cannot truncate a wide string. */
struct rows {
  int values[1][2];
};

int main(void) {
  struct rows value = {.values = {L"abc"}};
  return value.values[0][0];
}
