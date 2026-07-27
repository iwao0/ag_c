// sizeof follows the runtime VLA shape through selected pointer-to-VLA,
// nested pointer, and reversed-subscript expressions. Only the selected
// generic association may contribute evaluated VLA index expressions.
int main(void) {
  int planes = 2;
  int rows = 3;
  int columns = 5;
  int cube[planes][rows][columns];
  int (*pointer)[planes][rows][columns] = &cube;
  int (**handle)[planes][rows][columns] = &pointer;
  int (***triple)[planes][rows][columns] = &handle;

  unsigned long cube_size =
      (unsigned long)planes * rows * columns * sizeof(int);
  unsigned long plane_size =
      (unsigned long)rows * columns * sizeof(int);
  unsigned long row_size =
      (unsigned long)columns * sizeof(int);

  if (sizeof(_Generic(0, int: pointer[0], default: cube)) !=
      cube_size)
    return 1;
  if (sizeof(_Generic(
          0, int: pointer[0][0], default: cube[0])) !=
      plane_size)
    return 2;
  if (sizeof(_Generic(
          0, int: pointer[0][0][0], default: cube[0][0])) !=
      row_size)
    return 3;
  if (sizeof(_Generic(0, int: (*handle)[0], default: cube)) !=
      cube_size)
    return 4;
  if (sizeof(_Generic(
          0, int: (*handle)[0][0], default: cube[0])) !=
      plane_size)
    return 5;
  if (sizeof(_Generic(0, int: **handle, default: cube)) !=
      cube_size)
    return 6;
  if (sizeof(_Generic(0, int: ***triple, default: cube)) !=
      cube_size)
    return 7;
  if (sizeof(_Generic(0, int: 0[pointer], default: cube)) !=
      cube_size)
    return 8;
  if (sizeof(_Generic(
          0, int: 0[0[pointer]], default: cube[0])) !=
      plane_size)
    return 9;
  if (sizeof(_Generic(
          0, int: 0[0[0[pointer]]], default: cube[0][0])) !=
      row_size)
    return 10;

  int selected_index = 0;
  int unselected_index = 0;
  if (sizeof(_Generic(
          0, int: (*handle)[selected_index++],
          default: (*handle)[unselected_index++])) != cube_size)
    return 11;
  if (selected_index != 1 || unselected_index != 0)
    return 12;
  return 0;
}
