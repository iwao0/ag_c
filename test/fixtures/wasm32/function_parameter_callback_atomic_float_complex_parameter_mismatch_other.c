typedef unsigned int plain_callback_function(
    float _Complex value);

unsigned int consume_atomic_float_complex(
    plain_callback_function *callback) {
  return callback(0);
}
