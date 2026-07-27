/*
 * A generic selection that selects a function remains a function designator.
 * sizeof therefore cannot be applied to it. Expect ag_c E3117.
 */
static int selected_function(void) {
  return 0;
}

int main(void) {
  return (int)sizeof(
      _Generic(0, int: selected_function, default: 0));
}
