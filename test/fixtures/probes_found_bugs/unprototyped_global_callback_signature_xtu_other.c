static int unprototyped_global_callback_impl(int value) {
  return value;
}

int (*unprototyped_global_callback)(int) =
    unprototyped_global_callback_impl;
int (*unprototyped_global_callbacks[2])(int) = {
    unprototyped_global_callback_impl,
    unprototyped_global_callback_impl,
};
int (**unprototyped_global_callback_slot)(int) =
    &unprototyped_global_callback;
