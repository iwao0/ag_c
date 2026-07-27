// sizeof must use the runtime shape of the VLA expression selected by
// _Generic. This also covers reversed subscripting, where the pointer operand
// is on the right of [] rather than the left.
int main(void) {
  int planes = 2;
  int rows = 3;
  int columns = 5;
  int cube[planes][rows][columns];
  int fallback[2] = {0, 0};
  unsigned long plane_size =
      (unsigned long)rows * columns * sizeof(int);
  unsigned long row_size =
      (unsigned long)columns * sizeof(int);

  if (sizeof(_Generic(0, int: cube[0], default: fallback)) !=
      plane_size)
    return 1;
  if (sizeof(_Generic(
          0,
          int: _Generic(0L, long: cube[0][0], default: fallback),
          default: fallback)) != row_size)
    return 2;
  if (sizeof(_Generic(0, int: *cube, default: fallback)) !=
      plane_size)
    return 3;
  if (sizeof(_Generic(0, int: *(cube + 1), default: fallback)) !=
      plane_size)
    return 4;
  if (sizeof(_Generic(0, int: 0[cube], default: fallback)) !=
      plane_size)
    return 5;
  if (sizeof(_Generic(0, int: 0[0[cube]], default: fallback)) !=
      row_size)
    return 6;

  // Both subscript operands now contain VLA-derived syntax. Operand types,
  // rather than source order, identify cube as the pointer operand.
  cube[0][0][0] = 0;
  if (sizeof(_Generic(
          0, int: cube[0][0][0][cube], default: fallback)) !=
      plane_size)
    return 7;
  return 0;
}
