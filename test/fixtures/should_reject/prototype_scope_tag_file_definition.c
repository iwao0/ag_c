void consume(struct prototype_only *value);

struct prototype_only {
  int value;
};

void consume(struct prototype_only *value) {
  (void)value;
}

int main(void) {
  return 0;
}
