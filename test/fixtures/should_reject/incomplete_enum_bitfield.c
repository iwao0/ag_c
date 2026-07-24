/* Integer ABI representation does not make an incomplete enum complete. */
enum value;
struct record {
  enum value member : 1;
};
int main(void) { return 0; }
