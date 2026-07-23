static int backing[64];
static int extent_evaluations;
static int global_rows_storage[2][3] = {
    {1, 2, 3}, {4, 5, 6}};

extern int (*global_rows)[];
extern int (*global_rows)[3];
int (*global_rows)[3] = global_rows_storage;

static int note_extent(int value) {
  extent_evaluations++;
  return value;
}

static int redeclared_vla_row(
    int columns, int (*row)[columns]);

static int redeclared_vla_row(
    int columns, int (*row)[3]) {
  return columns == 3 ? (*row)[2] : -1;
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

static int nested_parameter_2d(
    int rows, int columns, int (**handle)[rows][columns]) {
  int index = 0;
  if ((int)sizeof (*handle)[index++] !=
          rows * columns * (int)sizeof(int) ||
      index != 1)
    return -1;
  index = 0;
  if ((int)sizeof (*handle)[index++][0] !=
          columns * (int)sizeof(int) ||
      index != 1)
    return -2;
  if ((int)sizeof **handle !=
      rows * columns * (int)sizeof(int))
    return -3;
  if ((int)sizeof (**handle)[0] !=
      columns * (int)sizeof(int))
    return -4;
  if ((int)sizeof (*handle)[0] !=
      rows * columns * (int)sizeof(int))
    return -5;
  if ((int)sizeof (*handle)[0][0] !=
      columns * (int)sizeof(int))
    return -6;
  return (*handle)[1][1][1];
}

static int call_nested_parameter_2d(int rows, int columns) {
  int (*matrix)[rows][columns] = (void *)backing;
  return nested_parameter_2d(rows, columns, &matrix);
}

static int nested_pointer_comparison(
    int columns, int (**runtime)[columns], int (**fixed)[3]) {
  return runtime == fixed;
}

static int call_nested_pointer_comparison(int columns) {
  int (*row)[3] = (void *)backing;
  return nested_pointer_comparison(columns, &row, &row);
}

static int nested_pointer_ops(
    int choose_runtime, int columns,
    int (**runtime)[columns], int (**fixed)[3]) {
  int (**selected)[columns] =
      choose_runtime ? runtime : fixed;
  if (selected != (choose_runtime ? runtime : fixed))
    return 0;
  return runtime - fixed == 0;
}

static int call_nested_pointer_ops(
    int choose_runtime, int columns) {
  int (*row)[3] = (void *)backing;
  return nested_pointer_ops(
      choose_runtime, columns, &row, &row);
}

static int triple_pointer_parameter(
    int columns, int (***triple)[columns]) {
  if ((int)sizeof ***triple !=
      columns * (int)sizeof(int))
    return -1;
  return (**triple)[1][2];
}

static int call_triple_pointer_parameter(int columns) {
  int (*row)[columns] = (void *)backing;
  int (**handle)[columns] = &row;
  return triple_pointer_parameter(columns, &handle);
}

static int generic_nested_vla_pointer(
    int columns, int (**handle)[columns]) {
  return _Generic(
      handle, int (**)[3]: 1, default: 0);
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
  if (call_nested_parameter_2d(2, 3) != 11) return 21;
  if (call_nested_parameter_2d(3, 4) != 18) return 22;
  if (!call_nested_pointer_comparison(3)) return 23;
  if (!call_nested_pointer_comparison(5)) return 24;
  if (!call_nested_pointer_ops(1, 3)) return 25;
  if (!call_nested_pointer_ops(0, 5)) return 26;
  if (call_triple_pointer_parameter(3) != 6) return 27;
  if (call_triple_pointer_parameter(5) != 8) return 28;
  {
    int row[3] = {4, 5, 6};
    if (redeclared_vla_row(3, &row) != 6) return 29;
    int (*pointer)[3] = &row;
    if (!generic_nested_vla_pointer(3, &pointer)) return 30;
    if (!generic_nested_vla_pointer(5, &pointer)) return 31;
  }
  if (global_rows[1][2] != 6) return 32;
  return 0;
}
