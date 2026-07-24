struct Pair {
  int first;
  int second;
};

int main(void) {
  register struct Pair pair = {1, 2};
  int *pointer = &pair.first;
  return *pointer;
}
