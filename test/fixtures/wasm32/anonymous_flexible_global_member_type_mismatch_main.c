typedef struct {
  int prefix;
  int payload[];
} anonymous_flexible_global_member_type_t;

extern anonymous_flexible_global_member_type_t
    anonymous_flexible_global_member_type_value;

int main(void) {
  return anonymous_flexible_global_member_type_value.prefix;
}
