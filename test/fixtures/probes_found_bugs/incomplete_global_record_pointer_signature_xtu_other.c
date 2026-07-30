struct incomplete_global_node {
  int value;
};

static struct incomplete_global_node incomplete_global_node_value = {
    42,
};
struct incomplete_global_node *incomplete_global_head =
    &incomplete_global_node_value;

int read_incomplete_global_head(void) {
  return incomplete_global_head->value;
}
