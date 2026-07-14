#ifndef AG_COMPILATION_SESSION_COMPAT_H
#define AG_COMPILATION_SESSION_COMPAT_H

#include "compilation_session.h"

/* Context-free APIs use these while legacy active-context entry points remain. */
ag_compilation_session_t *ag_compilation_session_active_compat(void);
ag_target_info_t ag_compilation_session_effective_target_compat(void);

#endif
