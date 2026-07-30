// The companion definition reuses this tag but changes the member type.
// Both aggregate parameters lower to the same Wasm i32 ABI value.

struct payload {
  int value;
};

int consume_payload(struct payload value);

int main(void) {
  struct payload value = {42};
  return consume_payload(value);
}
