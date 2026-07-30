int invoke_callback_factory(
    int (*(*callback)(void))(int), int value) {
  return callback()(value);
}
