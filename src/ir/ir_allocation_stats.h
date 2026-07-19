#ifndef AG_IR_ALLOCATION_STATS_H
#define AG_IR_ALLOCATION_STATS_H

#include <stddef.h>

typedef struct ir_allocation_stats_t {
  size_t instruction_live;
  size_t instruction_peak;
  size_t block_live;
  size_t block_peak;
} ir_allocation_stats_t;

void ir_allocation_stats_reset(ir_allocation_stats_t *stats);
size_t ir_allocation_stats_instruction_live(
    const ir_allocation_stats_t *stats);
size_t ir_allocation_stats_instruction_peak(
    const ir_allocation_stats_t *stats);
size_t ir_allocation_stats_block_live(
    const ir_allocation_stats_t *stats);
size_t ir_allocation_stats_block_peak(
    const ir_allocation_stats_t *stats);

#endif
