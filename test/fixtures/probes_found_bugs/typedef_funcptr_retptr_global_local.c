int value = 11;

int *get_value(void) {
  return &value;
}

int inc(int x) {
  return x + 1;
}

typedef int *IntPtr;
typedef int *(*Getter)(void);
typedef IntPtr (*GetterViaBase)(void);
typedef int (**IntFnPtrPtr)(int);

int (*global_fp)(int) = inc;
Getter global_getter = get_value;
GetterViaBase global_getter_via_base = get_value;
IntFnPtrPtr global_pp = &global_fp;

int main(void) {
  typedef int *(*LocalGetter)(void);
  typedef int (**LocalFnPtrPtr)(int);
  LocalGetter local_getter = get_value;
  LocalFnPtrPtr local_pp = &global_fp;
  int a = *global_getter();
  int b = *global_getter_via_base();
  int c = (*global_pp)(20);
  int d = *local_getter();
  int e = (*local_pp)(30);
  return (a == 11 && b == 11 && c == 21 && d == 11 && e == 31) ? 0 : 1;
}
