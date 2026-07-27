#ifndef AG_C_THREAD_LOCAL_XTU_BOUNDARIES_H
#define AG_C_THREAD_LOCAL_XTU_BOUNDARIES_H

struct thread_local_payload {
  int count;
  double value;
};

extern _Thread_local int shared_thread_value;
extern _Alignas(16) _Thread_local struct thread_local_payload
    shared_thread_payload;

int other_read_shared_thread_value(void);
int other_add_shared_thread_value(int value);
int *other_shared_thread_address(void);
int *other_private_thread_address(void);
struct thread_local_payload *other_thread_payload_address(void);

#endif
