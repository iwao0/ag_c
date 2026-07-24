struct value {
  int member;
};

int main(void) {
  struct value object = {7};
  return (int)object;
}
