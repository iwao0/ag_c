extern int other_value(void);

static int shared_name(void);
int shared_name(void) {
  return 35;
}

int main(void) {
  return shared_name() + other_value();
}
