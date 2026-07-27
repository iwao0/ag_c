// A generic selection preserves the selected VLA's runtime element shape
// until address-of, array decay, subscripting, or a function call consumes it.
static int sum_row(int columns, const int row[columns]) {
  int sum = 0;
  for (int i = 0; i < columns; i++) sum += row[i];
  return sum;
}

int main(void) {
  int rows = 2;
  int columns = 5;
  int matrix[rows][columns];
  int other[rows][columns];

  for (int row = 0; row < rows; row++) {
    for (int column = 0; column < columns; column++) {
      matrix[row][column] = 10 * row + column;
      other[row][column] = 100 + 10 * row + column;
    }
  }

  int (*selected_matrix)[columns] =
      _Generic(0, int: matrix, default: other);
  int (*selected_row_address)[columns] =
      &_Generic(0, int: matrix[1], default: other[0]);
  int *selected_row =
      _Generic(0, int: matrix[1], default: other[0]);
  int (*nested_row_address)[columns] =
      &_Generic(
          0,
          int: _Generic(
              0L, long: matrix[0], default: other[1]),
          default: other[0]);

  selected_matrix[0][4] = 44;
  (*selected_row_address)[3] = 33;
  (*nested_row_address)[2] = 22;

  if (selected_row != matrix[1]) return 1;
  if (selected_matrix[0][4] != 44) return 2;
  if (selected_matrix[1][3] != 33) return 3;
  if (matrix[0][2] != 22) return 4;
  if (sum_row(
          columns,
          _Generic(
              0, int: matrix[1], default: other[0])) !=
      10 + 11 + 12 + 33 + 14)
    return 5;
  return 0;
}
