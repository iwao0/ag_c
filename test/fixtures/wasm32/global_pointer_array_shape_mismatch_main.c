extern int (*global_pointer_array_shape_value)[2];

int main(void) {
  return (*global_pointer_array_shape_value)[1];
}
