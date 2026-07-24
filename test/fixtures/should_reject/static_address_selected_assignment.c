static int values[2];
static int side_effect;

static int *selected_assignment =
    0 ? &values[0] : (side_effect = 1, &values[1]);

int main(void) {
  return selected_assignment == values;
}
