/* A member of a selected register aggregate is still not addressable. */
struct pair {
  int first;
  int second;
};

int main(void) {
  register struct pair value = {1, 2};
  return &_Generic(
              0, int: value.first, default: value.second) != 0;
}
