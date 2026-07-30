static int global_unprototyped_narrow_callback_impl(char value) {
  return value;
}

int (*global_unprototyped_narrow_callback)(char) =
    global_unprototyped_narrow_callback_impl;
