int inspect_atomic_array_parameters(
    int pointer_atomic[const _Atomic 1],
    _Atomic(int) element_atomic[static const 1]);

int read_old_style_atomic_array(int *value);

typedef int atomic_array_callback(int value[_Atomic 1]);

static int apply_atomic_array_callback(
    atomic_array_callback *callback, int *value) {
  return callback(value);
}

static int read_plain_pointer(int *value) {
  return *value;
}

int main(void) {
  int pointer_value[1] = {10};
  _Atomic(int) element_value[1] = {11};
  int old_style_value[1] = {9};
  int callback_value = 12;
  return inspect_atomic_array_parameters(pointer_value, element_value) +
         read_old_style_atomic_array(old_style_value) +
         apply_atomic_array_callback(read_plain_pointer, &callback_value);
}
