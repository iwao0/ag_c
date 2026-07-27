// _Generic preserves selected register lvalues for value access and
// assignment. Unevaluated sizeof may inspect register arrays and aggregates
// without requiring an address.
struct pair {
  int first;
  int second;
};

int main(void) {
  register int value = 7;
  register struct pair record = {3, 4};
  register int values[3] = {1, 2, 3};
  int fallback[2] = {8, 9};

  _Generic(0, int: value, default: record.first) = 11;
  _Generic(0, int: record.second, default: value) = 12;
  if (_Generic(0, int: value, default: 0) != 11) return 1;
  if (_Generic(0, int: record.second, default: 0) != 12) return 2;
  if (sizeof(_Generic(0, int: values, default: fallback)) !=
      sizeof values)
    return 3;
  if (sizeof(_Generic(0, int: record, default: record)) !=
      sizeof record)
    return 4;
  return 0;
}
