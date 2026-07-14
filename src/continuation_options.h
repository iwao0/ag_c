#ifndef AG_CONTINUATION_OPTIONS_H
#define AG_CONTINUATION_OPTIONS_H

typedef struct {
  char *entry;
  char *frame_condition;
  char *start_export;
  char *resume_export;
  char *status_export;
  char *result_export;
  unsigned char enabled;
} ag_continuation_options_t;

#endif
