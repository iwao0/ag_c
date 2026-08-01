#ifndef AG_C_OVERALIGNED_VLA_CALLBACK_AGGREGATE_RETURN_XTU_H
#define AG_C_OVERALIGNED_VLA_CALLBACK_AGGREGATE_RETURN_XTU_H

struct aligned64_vla_return_cell {
  _Alignas(64) unsigned long long value;
  unsigned int tag;
};

struct aligned64_vla_callback_result {
  _Alignas(64) unsigned long long sum;
  unsigned long long count;
  unsigned long long edge;
};

typedef struct aligned64_vla_callback_result
    vla_aggregate_return_callback_t(
        int rows, int columns,
        const struct aligned64_vla_return_cell
            input[static restrict 1][*]);
typedef vla_aggregate_return_callback_t
    *vla_aggregate_return_factory_t(void);

vla_aggregate_return_callback_t
    *select_overaligned_vla_aggregate_return_callback(void);
extern vla_aggregate_return_factory_t
    *overaligned_vla_aggregate_return_factory;

#endif
