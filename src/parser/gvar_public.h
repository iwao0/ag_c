#ifndef PARSER_GVAR_PUBLIC_H
#define PARSER_GVAR_PUBLIC_H

#include "core.h"

typedef struct global_var_t global_var_t;

global_var_t *psx_find_global_var(char *name, int len);
int psx_gvar_is_extern_decl(const global_var_t *gv);
int psx_gvar_is_thread_local(const global_var_t *gv);
int psx_gvar_is_static_storage(const global_var_t *gv);
int psx_gvar_is_extern_decl_by_name(char *name, int len);
int psx_gvar_is_thread_local_by_name(char *name, int len);
int psx_gvar_is_static_storage_by_name(char *name, int len);
char *psx_gvar_name(const global_var_t *gv);
int psx_gvar_name_len(const global_var_t *gv);
psx_decl_funcptr_sig_t psx_gvar_funcptr_sig(const global_var_t *src);
psx_decl_funcptr_sig_t psx_gvar_funcptr_sig_by_name(char *name, int len);

#endif
