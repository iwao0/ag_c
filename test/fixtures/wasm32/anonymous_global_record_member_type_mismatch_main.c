typedef struct {
  int value;
  unsigned char tag;
} anonymous_global_record_member_type_t;

extern anonymous_global_record_member_type_t
    anonymous_global_record_member_type_value;

int main(void) {
  return anonymous_global_record_member_type_value.value;
}
