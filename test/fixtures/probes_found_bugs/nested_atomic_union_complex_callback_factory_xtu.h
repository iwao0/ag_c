#ifndef AG_C_NESTED_ATOMIC_UNION_COMPLEX_CALLBACK_FACTORY_XTU_H
#define AG_C_NESTED_ATOMIC_UNION_COMPLEX_CALLBACK_FACTORY_XTU_H

union factory_words3 {
  unsigned int words[3];
  unsigned char bytes[12];
};

typedef unsigned int atomic_union_target_function(
    const volatile _Atomic(union factory_words3) value);
typedef atomic_union_target_function *atomic_union_factory_function(void);

typedef _Atomic(double _Complex) atomic_complex_target_function(void);
typedef atomic_complex_target_function *atomic_complex_factory_function(void);

unsigned int invoke_atomic_union_factory(
    atomic_union_factory_function *factory);
unsigned int invoke_atomic_complex_factory(
    atomic_complex_factory_function *factory);

#endif
