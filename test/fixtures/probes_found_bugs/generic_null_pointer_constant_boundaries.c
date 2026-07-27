// A generic selection whose selected association is an integer constant
// expression with value zero remains a null pointer constant. This applies
// to explicit assignment as well as initialization, calls, returns,
// comparisons, and conditional expressions.
typedef int callback_type(int);

enum zero_value {
  ENUM_ZERO
};

static int add_one(int value) {
  return value + 1;
}

static int accepts_object_pointer(int *pointer) {
  return pointer == _Generic(0, int: 0, default: 1);
}

static int accepts_callback(callback_type *callback) {
  return callback == _Generic(0, int: 0, default: 1);
}

static int *returns_object_pointer(void) {
  return _Generic(0, int: 0, default: 1);
}

static callback_type *returns_callback(void) {
  return _Generic(0, int: 0, default: 1);
}

static int *global_object_pointer =
    _Generic(0, int: 0, default: 1);
static callback_type *global_callback =
    _Generic(0, int: 0, default: 1);

int main(void) {
  int value = 7;
  int control_effect = 0;
  int unselected_effect = 0;
  int *object_pointer = &value;
  callback_type *callback = add_one;

  object_pointer =
      _Generic(
          (control_effect++, 0),
          int: 0,
          default: (unselected_effect++, 1));
  if (object_pointer != 0 ||
      control_effect != 0 ||
      unselected_effect != 0)
    return 1;

  object_pointer =
      _Generic(0L, long: ENUM_ZERO, default: 1);
  if (object_pointer != 0) return 2;
  object_pointer =
      _Generic(0, int: 2 - 2, default: 1);
  if (object_pointer != 0) return 3;
  object_pointer =
      _Generic(
          0,
          int: _Generic(0L, long: 0, default: 1),
          default: 1);
  if (object_pointer != 0) return 4;
  object_pointer =
      _Generic(0, int: (int)0.0, default: 1);
  if (object_pointer != 0) return 5;

  callback = _Generic(0, int: 0, default: 1);
  if (callback != 0) return 6;
  if (!accepts_object_pointer(
          _Generic(0, int: 0, default: 1)))
    return 7;
  if (!accepts_callback(
          _Generic(0, int: 0, default: 1)))
    return 8;
  if (returns_object_pointer() != 0 ||
      returns_callback() != 0)
    return 9;
  if (global_object_pointer != 0 ||
      global_callback != 0)
    return 10;

  object_pointer =
      1 ? &value : _Generic(0, int: 0, default: 1);
  callback =
      1 ? add_one : _Generic(0, int: 0, default: 1);
  if (object_pointer != &value || callback(4) != 5)
    return 11;
  return 0;
}
