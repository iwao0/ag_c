int global_value = 7;

int *get_global(void) {
  return &global_value;
}

int *(*global_fp)(void) = get_global;

int read_param(int *(*fp)(void)) {
  return *fp();
}

struct Holder {
  int *(*fp)(void);
};

int read_member(struct Holder *h) {
  return *h->fp();
}

int main(void) {
  struct Holder h;
  h.fp = get_global;
  int a = *global_fp();
  int b = read_param(global_fp);
  int c = read_member(&h);
  return (a == 7 && b == 7 && c == 7) ? 0 : 1;
}
