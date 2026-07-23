static int backing[64];
static int extent_evaluations;

static int note_extent(int value) {
  extent_evaluations++;
  return value;
}

static int automatic_2d(int rows, int columns) {
  int (*pointer)[rows][columns] = (void *)backing;
  if ((int)sizeof pointer[0] != rows * columns * (int)sizeof(int))
    return -1;
  if ((int)sizeof pointer[0][0] != columns * (int)sizeof(int))
    return -2;
  return (*(pointer + 1))[1][1];
}

static int static_2d(int rows, int columns) {
  static int (*pointer)[rows][columns] = 0;
  pointer = (void *)backing;
  return pointer[1][1][1];
}

static int automatic_3d(int planes, int rows, int columns) {
  int (*pointer)[planes][rows][columns] = (void *)backing;
  return pointer[1][1][1][1];
}

static int static_mixed(int rows) {
  static int (*pointer)[rows][4] = 0;
  pointer = (void *)backing;
  return pointer[1][1][2];
}

static int parameter_2d(
    int rows, int columns, int (*pointer)[rows][columns]) {
  if ((int)sizeof pointer[0] != rows * columns * (int)sizeof(int))
    return -1;
  if ((int)sizeof pointer[0][0] != columns * (int)sizeof(int))
    return -2;
  return pointer[1][1][1];
}

static int automatic_bounds_once(void) {
  extent_evaluations = 0;
  int (*pointer)[note_extent(2)][note_extent(3)] =
      (void *)backing;
  return extent_evaluations == 2 && pointer[1][1][1] == 11;
}

static int static_bounds_each_entry(int rows, int columns) {
  static int (*pointer)[note_extent(rows)][note_extent(columns)] = 0;
  pointer = (void *)backing;
  return pointer[1][1][1];
}

static int nested_pointer_1d(int columns) {
  int (*row)[columns] = (void *)backing;
  int (**handle)[columns] = &row;
  if ((char *)(handle + 1) - (char *)handle != (int)sizeof row)
    return -1;
  if (handle[0][1][2] != (*handle)[1][2]) return -2;
  return (*handle)[1][2];
}

static int nested_pointer_2d(int rows, int columns) {
  int (*matrix)[rows][columns] = (void *)backing;
  int (**handle)[rows][columns] = &matrix;
  return handle[0][1][1][1];
}

static int static_nested_pointer_2d(int rows, int columns) {
  int (*matrix)[rows][columns] = (void *)backing;
  static int (**handle)[rows][columns] = 0;
  handle = &matrix;
  return (*handle)[1][1][1];
}

int main(void) {
  for (int i = 0; i < 64; i++) backing[i] = i + 1;

  if (automatic_2d(2, 3) != 11) return 1;
  if (automatic_2d(3, 4) != 18) return 2;
  if (static_2d(2, 3) != 11) return 3;
  if (static_2d(3, 4) != 18) return 4;
  if (automatic_3d(2, 2, 3) != 23) return 5;
  if (automatic_3d(2, 3, 4) != 42) return 6;
  if (static_mixed(2) != 15) return 7;
  if (static_mixed(3) != 19) return 8;
  if (parameter_2d(2, 3, (void *)backing) != 11) return 9;
  if (parameter_2d(3, 4, (void *)backing) != 18) return 10;
  if (!automatic_bounds_once()) return 11;
  extent_evaluations = 0;
  if (static_bounds_each_entry(2, 3) != 11) return 12;
  if (static_bounds_each_entry(3, 4) != 18) return 13;
  if (extent_evaluations != 4) return 14;
  if (nested_pointer_1d(3) != 6) return 15;
  if (nested_pointer_1d(5) != 8) return 16;
  if (nested_pointer_2d(2, 3) != 11) return 17;
  if (nested_pointer_2d(3, 4) != 18) return 18;
  if (static_nested_pointer_2d(2, 3) != 11) return 19;
  if (static_nested_pointer_2d(3, 4) != 18) return 20;
  return 0;
}
