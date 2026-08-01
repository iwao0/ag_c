#ifndef AG_C_OVERALIGNED_VLA_CALLBACK_FACTORY_XTU_H
#define AG_C_OVERALIGNED_VLA_CALLBACK_FACTORY_XTU_H

struct aligned64_vla_factory_cell {
  _Alignas(64) unsigned long long value;
  unsigned int tag;
};

typedef unsigned long long vla_factory_callback_t(
    int rows, int columns,
    const struct aligned64_vla_factory_cell
        input[static restrict 1][*]);
typedef vla_factory_callback_t *vla_callback_factory_t(void);

vla_factory_callback_t *select_overaligned_vla_callback(void);
extern vla_callback_factory_t
    *overaligned_vla_callback_factory;

#endif
