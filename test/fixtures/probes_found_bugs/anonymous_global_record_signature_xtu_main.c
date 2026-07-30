typedef struct {
  int value;
  unsigned char tag;
} anonymous_global_record_signature_t;

extern anonymous_global_record_signature_t
    anonymous_global_record_signature_value;

int main(void) {
  return anonymous_global_record_signature_value.value +
         anonymous_global_record_signature_value.tag;
}
