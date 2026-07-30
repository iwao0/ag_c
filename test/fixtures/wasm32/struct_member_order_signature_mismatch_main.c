struct ordered_pair {
  int first;
  int second;
};

int sum_ordered_pair(struct ordered_pair value);

int main(void) {
  struct ordered_pair value = {20, 22};
  return sum_ordered_pair(value) == 42 ? 0 : 1;
}
