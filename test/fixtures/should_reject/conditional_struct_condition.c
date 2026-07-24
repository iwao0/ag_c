struct condition_record {
  int value;
};

int main(void) {
  struct condition_record condition = {1};
  return condition ? 1 : 0;
}
