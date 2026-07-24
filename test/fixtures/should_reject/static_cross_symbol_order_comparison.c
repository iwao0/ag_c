/* Relational comparison between unrelated objects is not constant. */
static int first_scalar;
static int second_scalar;

static int cross_symbol_order =
    &first_scalar < &second_scalar;

int main(void) {
  return cross_symbol_order;
}
