// A non-const pointer parameter may initialize an addressed local object.
#include <assert.h>

struct Payload {
    int values[2];
    int (*operation)(int);
};

static int add_three(int value) {
    return value + 3;
}

static void initialize_payload(struct Payload *output) {
    output->values[0] = 17;
    output->values[1] = 22;
    output->operation = add_three;
}

int main(void) {
    struct Payload payload;

    initialize_payload(&payload);
    assert(payload.values[0] == 17);
    assert(payload.values[1] == 22);
    assert(payload.operation(payload.values[1]) == 25);
    return 0;
}
