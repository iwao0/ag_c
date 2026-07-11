#ifndef PARSER_NODE_VLA_PUBLIC_H
#define PARSER_NODE_VLA_PUBLIC_H

#include "node_fwd.h"

int ps_node_vla_alloc_descriptor_info(node_t *node, int *descriptor_frame_off,
                                       int *row_stride_frame_off);
int ps_node_vla_row_stride_frame_off(node_t *n);

#endif
