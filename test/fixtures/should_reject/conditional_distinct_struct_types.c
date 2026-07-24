struct first_record {
  int value;
};

struct second_record {
  int value;
};

int main(void) {
  struct first_record first = {1};
  struct second_record second = {2};
  (void)(1 ? first : second);
  return 0;
}
