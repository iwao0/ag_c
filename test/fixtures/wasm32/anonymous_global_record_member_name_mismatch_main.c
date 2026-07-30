typedef struct {
  int value;
  unsigned char tag;
} anonymous_global_record_member_name_t;

extern anonymous_global_record_member_name_t
    anonymous_global_record_member_name_value;

int main(void) {
  return anonymous_global_record_member_name_value.value;
}
