extern int other_value(void);

static int s = 30;
extern int s;

static int shared_name(void);
extern int shared_name(void);
int shared_name(void) {
  return s + 5;
}

int main(void) {
  return shared_name() + other_value();
}
