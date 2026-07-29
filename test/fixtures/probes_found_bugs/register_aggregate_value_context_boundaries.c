// Register aggregates remain usable as values even though their addresses
// cannot be taken explicitly.
// Expected: exit=0
#include <assert.h>

struct tiny {
  unsigned char bytes[3];
};

struct packet {
  long head;
  int body[3];
  long tail;
};

union word {
  unsigned long long bits;
  double real;
};

union envelope {
  struct packet packet;
  unsigned char bytes[sizeof(struct packet)];
};

static int tiny_sum(struct tiny value) {
  return value.bytes[0] + value.bytes[1] + value.bytes[2];
}

static long packet_sum(struct packet value) {
  return value.head + value.body[0] + value.body[1] +
         value.body[2] + value.tail;
}

static unsigned long long word_bits(union word value) {
  return value.bits;
}

static long envelope_sum(union envelope value) {
  return packet_sum(value.packet);
}

static struct tiny rotate_tiny(register struct tiny value) {
  register struct tiny result = {{
      value.bytes[1], value.bytes[2], value.bytes[0]}};
  return result;
}

static struct packet rotate_packet(
    register struct packet value) {
  register struct packet result = {
      value.tail,
      {value.body[2], value.body[0], value.body[1]},
      value.head};
  return result;
}

static union word copy_word(register union word value) {
  register union word result = value;
  return result;
}

static union envelope copy_envelope(
    register union envelope value) {
  register union envelope result = value;
  return result;
}

static struct tiny make_tiny(void) {
  register struct tiny value = {{17, 19, 23}};
  return value;
}

static struct packet make_packet(void) {
  register struct packet value = {
      29, {31, 37, 41}, 43};
  return value;
}

static union word make_word(void) {
  register union word value = {
      .bits = 0x1122334455667788ULL};
  return value;
}

static union envelope make_envelope(void) {
  register union envelope value = {
      .packet = {47, {53, 59, 61}, 67}};
  return value;
}

static void verify_initialization_assignment_and_calls(void) {
  register const struct tiny constant_tiny = {{2, 3, 5}};
  register volatile struct tiny varying_tiny = {{7, 11, 13}};
  register const volatile struct tiny fixed_varying_tiny = {{
      17, 19, 23}};

  struct tiny initialized_tiny = constant_tiny;
  struct tiny assigned_tiny = {{0, 0, 0}};
  struct tiny assignment_result =
      (assigned_tiny = varying_tiny);
  assert(tiny_sum(initialized_tiny) == 10);
  assert(tiny_sum(assigned_tiny) == 31);
  assert(tiny_sum(assignment_result) == 31);
  assert(tiny_sum(fixed_varying_tiny) == 59);
  assert(tiny_sum(rotate_tiny(constant_tiny)) == 10);

  register const struct packet constant_packet = {
      71, {73, 79, 83}, 89};
  register volatile struct packet varying_packet = {
      97, {101, 103, 107}, 109};
  register const volatile struct packet fixed_varying_packet = {
      113, {127, 131, 137}, 139};

  struct packet initialized_packet = constant_packet;
  struct packet assigned_packet = {0, {0, 0, 0}, 0};
  struct packet assignment_packet_result =
      (assigned_packet = varying_packet);
  assert(packet_sum(initialized_packet) == 395);
  assert(packet_sum(assigned_packet) == 517);
  assert(packet_sum(assignment_packet_result) == 517);
  assert(packet_sum(fixed_varying_packet) == 647);
  assert(packet_sum(rotate_packet(constant_packet)) == 395);

  register const union word constant_word = {
      .bits = 0x89abcdef01234567ULL};
  register volatile union word varying_word = {
      .bits = 0xfedcba9876543210ULL};
  register const volatile union word fixed_varying_word = {
      .bits = 0xa5a55a5af0f00f0fULL};

  union word initialized_word = constant_word;
  union word assigned_word = {.bits = 0};
  union word assignment_word_result =
      (assigned_word = varying_word);
  assert(word_bits(initialized_word) ==
         0x89abcdef01234567ULL);
  assert(word_bits(assigned_word) ==
         0xfedcba9876543210ULL);
  assert(word_bits(assignment_word_result) ==
         0xfedcba9876543210ULL);
  assert(word_bits(fixed_varying_word) ==
         0xa5a55a5af0f00f0fULL);
  assert(word_bits(copy_word(constant_word)) ==
         0x89abcdef01234567ULL);

  register const union envelope constant_envelope = {
      .packet = {149, {151, 157, 163}, 167}};
  register volatile union envelope varying_envelope = {
      .packet = {173, {179, 181, 191}, 193}};
  register const volatile union envelope fixed_varying_envelope = {
      .packet = {197, {199, 211, 223}, 227}};

  union envelope initialized_envelope = constant_envelope;
  union envelope assigned_envelope = {
      .packet = {0, {0, 0, 0}, 0}};
  union envelope assignment_envelope_result =
      (assigned_envelope = varying_envelope);
  assert(envelope_sum(initialized_envelope) == 787);
  assert(envelope_sum(assigned_envelope) == 917);
  assert(envelope_sum(assignment_envelope_result) == 917);
  assert(envelope_sum(fixed_varying_envelope) == 1057);
  assert(envelope_sum(copy_envelope(constant_envelope)) == 787);
}

static void verify_returns_conditionals_and_comma(void) {
  assert(tiny_sum(make_tiny()) == 59);
  assert(packet_sum(make_packet()) == 181);
  assert(word_bits(make_word()) == 0x1122334455667788ULL);
  assert(envelope_sum(make_envelope()) == 287);

  register struct tiny tiny_left = {{2, 3, 5}};
  register struct tiny tiny_right = {{7, 11, 13}};
  register struct packet packet_left = {
      17, {19, 23, 29}, 31};
  register struct packet packet_right = {
      37, {41, 43, 47}, 53};
  register union word word_left = {
      .bits = 0x0102030405060708ULL};
  register union word word_right = {
      .bits = 0x8070605040302010ULL};
  register union envelope envelope_left = {
      .packet = {59, {61, 67, 71}, 73}};
  register union envelope envelope_right = {
      .packet = {79, {83, 89, 97}, 101}};
  int choose_left = 1;
  int comma_evaluations = 0;

  struct tiny selected_tiny =
      choose_left ? tiny_left : tiny_right;
  struct packet selected_packet =
      choose_left ? packet_left : packet_right;
  union word selected_word =
      choose_left ? word_left : word_right;
  union envelope selected_envelope =
      choose_left ? envelope_left : envelope_right;
  assert(tiny_sum(selected_tiny) == 10);
  assert(packet_sum(selected_packet) == 119);
  assert(word_bits(selected_word) == 0x0102030405060708ULL);
  assert(envelope_sum(selected_envelope) == 331);

  choose_left = 0;
  selected_tiny = choose_left ? tiny_left : tiny_right;
  selected_packet =
      choose_left ? packet_left : packet_right;
  selected_word = choose_left ? word_left : word_right;
  selected_envelope =
      choose_left ? envelope_left : envelope_right;
  assert(tiny_sum(selected_tiny) == 31);
  assert(packet_sum(selected_packet) == 221);
  assert(word_bits(selected_word) == 0x8070605040302010ULL);
  assert(envelope_sum(selected_envelope) == 449);

  struct tiny comma_tiny =
      (comma_evaluations++, tiny_left);
  struct packet comma_packet =
      (comma_evaluations++, packet_right);
  union word comma_word =
      (comma_evaluations++, word_left);
  union envelope comma_envelope =
      (comma_evaluations++, envelope_right);
  assert(tiny_sum(comma_tiny) == 10);
  assert(packet_sum(comma_packet) == 221);
  assert(word_bits(comma_word) == 0x0102030405060708ULL);
  assert(envelope_sum(comma_envelope) == 449);
  assert(comma_evaluations == 4);
}

int main(void) {
  verify_initialization_assignment_and_calls();
  verify_returns_conditionals_and_comma();
  return 0;
}
