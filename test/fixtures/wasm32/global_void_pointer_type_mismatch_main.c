extern void *global_void_pointer_type_value;

int main(void) {
  return *(int *)global_void_pointer_type_value;
}
