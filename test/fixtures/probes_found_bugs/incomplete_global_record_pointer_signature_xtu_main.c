struct incomplete_global_node;

extern struct incomplete_global_node *incomplete_global_head;
int read_incomplete_global_head(void);

int main(void) {
  return incomplete_global_head != 0 &&
                 read_incomplete_global_head() == 42
             ? 0
             : 1;
}
