/* A member of an aggregate function result is not an lvalue in C11. */
struct pair {
  int value;
};

static struct pair make_pair(void) {
  return (struct pair){7};
}

int main(void) {
  return &make_pair().value != 0;
}
