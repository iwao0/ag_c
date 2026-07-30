struct ordered_pair {
  int second;
  int first;
};

int sum_ordered_pair(struct ordered_pair value) {
  return value.first + value.second;
}
