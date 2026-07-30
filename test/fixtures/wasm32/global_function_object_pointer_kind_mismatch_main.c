typedef int global_function_object_pointer_kind_callback_t(void);

extern global_function_object_pointer_kind_callback_t
    *global_function_object_pointer_kind_value;

int main(void) {
  return global_function_object_pointer_kind_value();
}
