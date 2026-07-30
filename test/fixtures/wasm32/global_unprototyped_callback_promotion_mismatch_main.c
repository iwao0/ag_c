typedef int global_unprototyped_narrow_callback_t();

extern global_unprototyped_narrow_callback_t
    *global_unprototyped_narrow_callback;

int main(void) {
  return global_unprototyped_narrow_callback(42);
}
