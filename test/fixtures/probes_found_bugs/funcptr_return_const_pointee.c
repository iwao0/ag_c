struct S {
  int x;
};

const struct S const_arr[1] = {{7}};
struct S mut = {1};

const struct S (*get_const_arr(void))[1] {
  return &const_arr;
}

struct S *get_mut(void) {
  return &mut;
}

int main(void) {
  const struct S (*(*fp_arr)(void))[1] = get_const_arr;
  if ((*fp_arr())[0].x != 7) return 1;

  struct S *(*fp_mut)(void) = get_mut;
  fp_mut()->x = 11;
  if (mut.x != 11) return 2;

  return 0;
}
