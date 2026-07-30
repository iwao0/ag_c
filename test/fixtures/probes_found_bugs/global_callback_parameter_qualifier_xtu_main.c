typedef int global_callback_parameter_qualifier_t(
    const int value, int pointer[static const restrict 1],
    int variable_length_pointer[const restrict *]);

extern global_callback_parameter_qualifier_t
    *global_callback_parameter_qualifier;

int main(void) {
  int extra = 2;
  int unused_extra = 0;
  return global_callback_parameter_qualifier(
      40, &extra, &unused_extra);
}
