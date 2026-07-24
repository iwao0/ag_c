#include <assert.h>
#include <stddef.h>

static int check_vla_parameter(int rows, int columns,
                               int values[rows][columns]) {
  assert(sizeof(values[0]) == (size_t)columns * sizeof(int));
  int total = 0;
  for (int row = 0; row < rows; ++row) {
    for (int column = 0; column < columns; ++column) {
      total += values[row][column];
    }
  }

  int (*row_pointer)[columns] = values;
  assert(sizeof *row_pointer == (size_t)columns * sizeof(int));
  assert(row_pointer[1][2] == values[1][2]);
  return total;
}

static int check_character_literals(void) {
  int characters[] = {
      '\n', '\0', '\x41', '\101', '\\', '\'', '\"', '\t',
  };
  assert(characters[0] == 10);
  assert(characters[1] == 0);
  assert(characters[2] == 65);
  assert(characters[3] == 65);
  assert(characters[4] == 92);
  assert(characters[5] == 39);
  assert(characters[6] == 34);
  assert(characters[7] == 9);

  char string[] = "a\n\0" "\x41" "\101";
  assert(sizeof(string) == 6);
  assert(string[0] == 'a');
  assert(string[1] == '\n');
  assert(string[2] == '\0');
  assert(string[3] == 'A');
  assert(string[4] == 'A');
  assert(string[5] == '\0');

  assert(L'A' == 65);
  assert(L'\n' == 10);
  assert(L"wide"[0] == L'w');
  assert(L"wide"[3] == L'e');
  assert(L"wide"[4] == 0);
  return 0;
}

static int check_local_vlas(int length, int rows, int columns) {
  int one_dimension[length];
  for (int i = 0; i < length; ++i) {
    one_dimension[i] = i + 1;
  }
  assert(sizeof(one_dimension) == (size_t)length * sizeof(int));
  assert(one_dimension[length - 1] == length);

  int matrix[rows][columns];
  int next = 1;
  for (int row = 0; row < rows; ++row) {
    for (int column = 0; column < columns; ++column) {
      matrix[row][column] = next++;
    }
  }
  assert(sizeof(matrix) ==
         (size_t)rows * (size_t)columns * sizeof(int));
  assert(sizeof(matrix[0]) == (size_t)columns * sizeof(int));
  assert(check_vla_parameter(rows, columns, matrix) == 21);

  int evaluated_bound = 3;
  int side_effect_vla[evaluated_bound++];
  assert(evaluated_bound == 4);
  assert(sizeof(side_effect_vla) == 3 * sizeof(int));
  return 0;
}

int main(void) {
  assert(check_character_literals() == 0);
  assert(check_local_vlas(5, 2, 3) == 0);
  return 0;
}
