typedef struct {
  int prefix;
  int payload[];
} anonymous_flexible_global_signature_t;

extern anonymous_flexible_global_signature_t
    anonymous_flexible_global_signature_value;

int main(void) {
  return anonymous_flexible_global_signature_value.prefix;
}
