int main(void) {
  int value = 1;
  int *object_pointer = &value;
  void *void_pointer = object_pointer;
  return sizeof(object_pointer < void_pointer);
}
