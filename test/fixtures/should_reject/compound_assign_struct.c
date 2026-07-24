struct Pair {
  int first;
  int second;
};

int main(void) {
  struct Pair value = {1, 2};
  value += value;
  return 0;
}
