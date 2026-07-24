/* Only the selected generic association is evaluated or contributes to an
 * integer constant expression; every association is still type-checked. */
#include <assert.h>

static int runtime_value(void) {
  return 99;
}

enum {
  GENERIC_ENUM_CONSTANT =
      _Generic(1, int: 7, default: runtime_value()),
  GENERIC_ENUM_DEFAULT =
      _Generic(1.0, int: runtime_value(), default: 9)
};

_Static_assert(
    _Generic(1, int: 1, default: runtime_value()),
    "selected association is an integer constant");
_Static_assert(
    _Generic(1.0, int: runtime_value(), default: 2) == 2,
    "selected default is an integer constant");

static int selected_integer =
    _Generic(1, int: 11, default: runtime_value());
static int selected_default =
    _Generic(1.0, int: runtime_value(), default: 13);
static int selected_array[] = {
    _Generic(1, int: 17, default: runtime_value()),
    _Generic(1.0, int: runtime_value(), default: 19)
};

int main(void) {
  int control_effect = 0;
  int unselected_effect = 0;
  int selected = _Generic(
      (control_effect++, 1),
      int: 23,
      default: (unselected_effect++, runtime_value()));

  assert(GENERIC_ENUM_CONSTANT == 7);
  assert(GENERIC_ENUM_DEFAULT == 9);
  assert(selected_integer == 11);
  assert(selected_default == 13);
  assert(selected_array[0] == 17);
  assert(selected_array[1] == 19);
  assert(control_effect == 0);
  assert(unselected_effect == 0);
  assert(selected == 23);
  return 0;
}
