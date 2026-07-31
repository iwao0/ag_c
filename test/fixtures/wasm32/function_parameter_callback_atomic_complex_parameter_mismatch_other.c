typedef unsigned int plain_callback_function(
    double _Complex value);

unsigned int consume_atomic_complex(
    plain_callback_function *callback) {
  return callback(0);
}
