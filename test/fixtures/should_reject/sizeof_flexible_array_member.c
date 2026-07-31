/* A flexible array member has incomplete type and has no sizeof value. */
struct packet {
  int length;
  unsigned char payload[];
};

int main(void) {
  struct packet *packet = 0;
  return (int)sizeof(packet->payload);
}
