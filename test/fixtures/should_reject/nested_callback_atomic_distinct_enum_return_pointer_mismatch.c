enum left_state {
  LEFT_STATE_NEGATIVE = -1,
  LEFT_STATE_VALUE = 42
};

enum right_state {
  RIGHT_STATE_NEGATIVE = -1,
  RIGHT_STATE_VALUE = 42
};

typedef _Atomic(enum left_state) left_callback_function(void);
typedef _Atomic(enum right_state) right_callback_function(void);
typedef int left_consumer_function(
    left_callback_function *callback);

static int consume_callback(
    right_callback_function *callback) {
  _Atomic(enum right_state) value = callback();
  return value;
}

int main(void) {
  left_consumer_function *invalid = consume_callback;
  return invalid == 0;
}
