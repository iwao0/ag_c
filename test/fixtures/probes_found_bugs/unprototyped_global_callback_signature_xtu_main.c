typedef int unprototyped_global_callback_t();

extern unprototyped_global_callback_t
    *unprototyped_global_callback;
extern unprototyped_global_callback_t
    *unprototyped_global_callbacks[2];
extern unprototyped_global_callback_t
    **unprototyped_global_callback_slot;

int main(void) {
  return unprototyped_global_callback(40) == 40 &&
                 unprototyped_global_callbacks[1](41) == 41 &&
                 (*unprototyped_global_callback_slot)(42) == 42
             ? 0
             : 1;
}
