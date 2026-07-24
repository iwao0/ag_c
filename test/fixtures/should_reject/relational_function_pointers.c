static int first(int value) {
  return value;
}

static int second(int value) {
  return value + 1;
}

int main(void) {
  return sizeof(first < second);
}
