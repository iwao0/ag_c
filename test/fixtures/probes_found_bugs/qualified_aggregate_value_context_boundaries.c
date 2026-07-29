// Top-level-qualified aggregate lvalues must be converted to unqualified
// aggregate values in initialization, assignment, calls, returns, and the
// conditional and comma operators.
// Expected: exit=0
#include <assert.h>

struct pair {
  int left;
  int right;
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

static const struct pair constant_pair = {11, 13};
static volatile struct pair varying_pair = {17, 19};
static const volatile struct pair fixed_varying_pair = {23, 29};

static const struct packet constant_packet = {
    31, {37, 41, 43}, 47};
static volatile struct packet varying_packet = {
    53, {59, 61, 67}, 71};
static const volatile struct packet fixed_varying_packet = {
    73, {79, 83, 89}, 97};

static const union word constant_word = {
    .bits = 0x1122334455667788ULL};
static volatile union word varying_word = {
    .bits = 0x8877665544332211ULL};
static const volatile union word fixed_varying_word = {
    .bits = 0xa5a55a5af0f00f0fULL};

static const union envelope constant_envelope = {
    .packet = {101, {103, 107, 109}, 113}};
static volatile union envelope varying_envelope = {
    .packet = {127, {131, 137, 139}, 149}};
static const volatile union envelope fixed_varying_envelope = {
    .packet = {151, {157, 163, 167}, 173}};

static int pair_left_evaluations;
static int pair_right_evaluations;
static int packet_left_evaluations;
static int packet_right_evaluations;
static int envelope_left_evaluations;
static int envelope_right_evaluations;
static int comma_evaluations;

static const struct pair *select_constant_pair(void) {
  pair_left_evaluations++;
  return &constant_pair;
}

static volatile struct pair *select_varying_pair(void) {
  pair_right_evaluations++;
  return &varying_pair;
}

static const struct packet *select_constant_packet(void) {
  packet_left_evaluations++;
  return &constant_packet;
}

static const volatile struct packet *select_fixed_varying_packet(void) {
  packet_right_evaluations++;
  return &fixed_varying_packet;
}

static const union envelope *select_constant_envelope(void) {
  envelope_left_evaluations++;
  return &constant_envelope;
}

static volatile union envelope *select_varying_envelope(void) {
  envelope_right_evaluations++;
  return &varying_envelope;
}

static int pair_sum(struct pair value) {
  return value.left + value.right;
}

static int qualified_pair_sum(
    const volatile struct pair value) {
  return value.left + value.right;
}

static long packet_sum(struct packet value) {
  return value.head + value.body[0] + value.body[1] +
         value.body[2] + value.tail;
}

static long qualified_packet_sum(
    const volatile struct packet value) {
  return value.head + value.body[0] + value.body[1] +
         value.body[2] + value.tail;
}

static unsigned long long word_bits(union word value) {
  return value.bits;
}

static long envelope_sum(union envelope value) {
  return packet_sum(value.packet);
}

static long qualified_envelope_sum(
    const volatile union envelope value) {
  return value.packet.head + value.packet.body[0] +
         value.packet.body[1] + value.packet.body[2] +
         value.packet.tail;
}

static struct pair return_varying_pair(void) {
  return varying_pair;
}

static struct packet return_fixed_varying_packet(void) {
  return fixed_varying_packet;
}

static union word return_fixed_varying_word(void) {
  return fixed_varying_word;
}

static union envelope return_varying_envelope(void) {
  return varying_envelope;
}

static void verify_initialization_and_calls(void) {
  struct pair from_const = constant_pair;
  struct pair from_volatile = varying_pair;
  struct pair from_cv = fixed_varying_pair;
  assert(pair_sum(from_const) == 24);
  assert(pair_sum(from_volatile) == 36);
  assert(pair_sum(from_cv) == 52);
  assert(pair_sum(constant_pair) == 24);
  assert(pair_sum(varying_pair) == 36);
  assert(qualified_pair_sum(fixed_varying_pair) == 52);

  struct packet packet_from_const = constant_packet;
  struct packet packet_from_volatile = varying_packet;
  struct packet packet_from_cv = fixed_varying_packet;
  assert(packet_sum(packet_from_const) == 199);
  assert(packet_sum(packet_from_volatile) == 311);
  assert(packet_sum(packet_from_cv) == 421);
  assert(packet_sum(constant_packet) == 199);
  assert(packet_sum(varying_packet) == 311);
  assert(qualified_packet_sum(fixed_varying_packet) == 421);

  union word word_from_const = constant_word;
  union word word_from_volatile = varying_word;
  union word word_from_cv = fixed_varying_word;
  assert(word_bits(word_from_const) == 0x1122334455667788ULL);
  assert(word_bits(word_from_volatile) == 0x8877665544332211ULL);
  assert(word_bits(word_from_cv) == 0xa5a55a5af0f00f0fULL);
  assert(word_bits(constant_word) == 0x1122334455667788ULL);
  assert(word_bits(varying_word) == 0x8877665544332211ULL);

  union envelope envelope_from_const = constant_envelope;
  union envelope envelope_from_volatile = varying_envelope;
  union envelope envelope_from_cv = fixed_varying_envelope;
  assert(envelope_sum(envelope_from_const) == 533);
  assert(envelope_sum(envelope_from_volatile) == 683);
  assert(envelope_sum(envelope_from_cv) == 811);
  assert(envelope_sum(constant_envelope) == 533);
  assert(envelope_sum(varying_envelope) == 683);
  assert(qualified_envelope_sum(fixed_varying_envelope) == 811);
}

static void verify_assignment_and_return(void) {
  struct pair pair_target = {0, 0};
  struct pair pair_result = (pair_target = varying_pair);
  assert(pair_sum(pair_target) == 36);
  assert(pair_sum(pair_result) == 36);
  pair_target = fixed_varying_pair;
  assert(pair_sum(pair_target) == 52);
  assert(pair_sum(return_varying_pair()) == 36);

  struct packet packet_target = {0, {0, 0, 0}, 0};
  struct packet packet_result =
      (packet_target = fixed_varying_packet);
  assert(packet_sum(packet_target) == 421);
  assert(packet_sum(packet_result) == 421);
  packet_target = varying_packet;
  assert(packet_sum(packet_target) == 311);
  assert(packet_sum(return_fixed_varying_packet()) == 421);

  union word word_target = {.bits = 0};
  union word word_result = (word_target = varying_word);
  assert(word_bits(word_target) == 0x8877665544332211ULL);
  assert(word_bits(word_result) == 0x8877665544332211ULL);
  word_target = fixed_varying_word;
  assert(word_bits(word_target) == 0xa5a55a5af0f00f0fULL);
  assert(word_bits(return_fixed_varying_word()) ==
         0xa5a55a5af0f00f0fULL);

  union envelope envelope_target = {
      .packet = {0, {0, 0, 0}, 0}};
  union envelope envelope_result =
      (envelope_target = varying_envelope);
  assert(envelope_sum(envelope_target) == 683);
  assert(envelope_sum(envelope_result) == 683);
  envelope_target = fixed_varying_envelope;
  assert(envelope_sum(envelope_target) == 811);
  assert(envelope_sum(return_varying_envelope()) == 683);
}

static void verify_conditional_and_comma(void) {
  int choose_left = 1;
  struct pair pair_left =
      choose_left ? *select_constant_pair()
                  : *select_varying_pair();
  assert(pair_sum(pair_left) == 24);
  assert(pair_left_evaluations == 1);
  assert(pair_right_evaluations == 0);

  choose_left = 0;
  struct pair pair_right =
      choose_left ? *select_constant_pair()
                  : *select_varying_pair();
  assert(pair_sum(pair_right) == 36);
  assert(pair_left_evaluations == 1);
  assert(pair_right_evaluations == 1);

  choose_left = 1;
  struct packet packet_left =
      choose_left ? *select_constant_packet()
                  : *select_fixed_varying_packet();
  assert(packet_sum(packet_left) == 199);
  assert(packet_left_evaluations == 1);
  assert(packet_right_evaluations == 0);

  choose_left = 0;
  struct packet packet_right =
      choose_left ? *select_constant_packet()
                  : *select_fixed_varying_packet();
  assert(packet_sum(packet_right) == 421);
  assert(packet_left_evaluations == 1);
  assert(packet_right_evaluations == 1);

  choose_left = 1;
  union envelope envelope_left =
      choose_left ? *select_constant_envelope()
                  : *select_varying_envelope();
  assert(envelope_sum(envelope_left) == 533);
  assert(envelope_left_evaluations == 1);
  assert(envelope_right_evaluations == 0);

  choose_left = 0;
  union envelope envelope_right =
      choose_left ? *select_constant_envelope()
                  : *select_varying_envelope();
  assert(envelope_sum(envelope_right) == 683);
  assert(envelope_left_evaluations == 1);
  assert(envelope_right_evaluations == 1);

  struct pair comma_pair =
      (comma_evaluations++, fixed_varying_pair);
  struct packet comma_packet =
      (comma_evaluations++, varying_packet);
  union word comma_word =
      (comma_evaluations++, fixed_varying_word);
  union envelope comma_envelope =
      (comma_evaluations++, fixed_varying_envelope);
  assert(pair_sum(comma_pair) == 52);
  assert(packet_sum(comma_packet) == 311);
  assert(word_bits(comma_word) == 0xa5a55a5af0f00f0fULL);
  assert(envelope_sum(comma_envelope) == 811);
  assert(comma_evaluations == 4);
}

int main(void) {
  verify_initialization_and_calls();
  verify_assignment_and_return();
  verify_conditional_and_comma();
  return 0;
}
