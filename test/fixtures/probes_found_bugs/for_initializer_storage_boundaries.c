int main(void) {
  int total = 0;

  for (auto int first = 0; first < 3; ++first) {
    total += first;
  }
  for (register int second = 0; second < 3; ++second) {
    total += second;
  }

  return total != 6;
}
