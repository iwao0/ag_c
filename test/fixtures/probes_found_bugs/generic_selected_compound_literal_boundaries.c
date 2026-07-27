// _Generic preserves selected string and compound literal value categories
// for address-of, array decay, direct assignment, and aggregate access. Only
// the selected compound literal initializer is evaluated.
struct pair {
  int first;
  int second;
};

int main(void) {
  int selected_effect = 0;
  int unselected_effect = 0;

  char (*text)[4] =
      &_Generic(0, int: "abc", default: "x");
  int (*array_address)[3] =
      &_Generic(
          0, int: (int[3]){1, 2, 3},
          default: (int[2]){4, 5});
  int *array_decay =
      _Generic(
          0, int: (int[3]){6, 7, 8},
          default: (int[2]){9, 10});
  int *evaluated =
      _Generic(
          0, int: (int[2]){++selected_effect, 12},
          default: (int[2]){++unselected_effect, 13});
  int (*nested_address)[2] =
      &_Generic(
          0,
          int: _Generic(
              0L, long: (int[2]){14, 15},
              default: (int[1]){16}),
          default: (int[1]){17});
  struct pair *record =
      &_Generic(
          0, int: (struct pair){18, 19},
          default: (struct pair){20, 21});
  int *scalar =
      &_Generic(0, int: (int){22}, default: (int){23});

  (*array_address)[1] = 30;
  record->second = 31;
  *scalar = 32;

  if ((*text)[0] != 'a' || (*text)[3] != '\0') return 1;
  if ((*array_address)[0] != 1 ||
      (*array_address)[1] != 30 ||
      (*array_address)[2] != 3)
    return 2;
  if (array_decay[0] != 6 || array_decay[2] != 8) return 3;
  if (selected_effect != 1 || unselected_effect != 0) return 4;
  if (evaluated[0] != 1 || evaluated[1] != 12) return 5;
  if ((*nested_address)[0] != 14 ||
      (*nested_address)[1] != 15)
    return 6;
  if (record->first != 18 || record->second != 31) return 7;
  if (*scalar != 32) return 8;

  struct pair replacement = {40, 41};
  if ((_Generic(
           0, int: (struct pair){33, 34},
           default: (struct pair){35, 36}) = replacement).second !=
      41)
    return 9;
  if ((_Generic(
           0, int: (int){37}, default: (int){38}) = 42) != 42)
    return 10;
  return 0;
}
