struct incomplete_global_record_object;

extern struct incomplete_global_record_object
    incomplete_global_record_value;
int check_incomplete_global_record_address(const void *address);
int read_incomplete_global_record_value(void);

int main(void) {
  return check_incomplete_global_record_address(
             &incomplete_global_record_value) &&
                 read_incomplete_global_record_value() == 42
             ? 0
             : 1;
}
