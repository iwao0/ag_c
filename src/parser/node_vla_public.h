#ifndef PARSER_NODE_VLA_PUBLIC_H
#define PARSER_NODE_VLA_PUBLIC_H

#include "node_fwd.h"
#include "vla_runtime.h"

int ps_node_vla_alloc_descriptor_info(node_t *node, int *descriptor_frame_off,
                                       int *row_stride_frame_off);
int ps_node_vla_row_stride_frame_off(node_t *n);
int ps_node_vla_strides_remaining(node_t *n);
psx_vla_runtime_view_t ps_node_vla_runtime_view(const node_t *node);
void ps_node_set_vla_runtime_view(node_t *node, int row_stride_frame_off,
                                  int strides_remaining);

#endif
