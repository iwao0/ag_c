typedef int cube_three[][3];
typedef int cube_four[][4];
typedef cube_three *factory_three(void);
typedef cube_four *factory_four(void);

static int consume_factory(factory_three *factory) {
  return (*factory())[0][0];
}

int main(void) {
  int (*invalid)(factory_four *) = consume_factory;
  return invalid == 0;
}
