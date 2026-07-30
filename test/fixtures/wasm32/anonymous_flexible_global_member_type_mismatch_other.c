typedef struct {
  int prefix;
  unsigned int payload[];
} anonymous_flexible_global_member_type_t;

anonymous_flexible_global_member_type_t
    anonymous_flexible_global_member_type_value = {.prefix = 42};
