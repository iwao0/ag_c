// Static void-pointer initializers that are not null pointer constants must
// retain their string/global relocations. Null-pointer classification must
// not rematerialize or renumber the selected initializer expression.
static int global_value = 37;
static void *global_text_pointer = (void *)"abc";
static void *global_object_pointer = (void *)&global_value;
static void *global_generic_text_pointer =
    _Generic(0, int: (void *)"xyz", default: (void *)0);

int main(void) {
  if (((char *)global_text_pointer)[0] != 'a' ||
      ((char *)global_text_pointer)[2] != 'c')
    return 1;
  if (*(int *)global_object_pointer != 37)
    return 2;
  if (((char *)global_generic_text_pointer)[1] != 'y')
    return 3;
  return 0;
}
