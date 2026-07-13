#define AG_RT_FE_ALL_EXCEPT 0x1f
#define AG_RT_FE_DFL_ENV_ADDR (-1L)
#define AG_RT_FE_TONEAREST 0x00000000
#define AG_RT_FE_UPWARD 0x00400000
#define AG_RT_FE_DOWNWARD 0x00800000
#define AG_RT_FE_TOWARDZERO 0x00C00000

static int ag_rt_fenv_is_dfl_env(long envp_addr) {
  return envp_addr == AG_RT_FE_DFL_ENV_ADDR ||
         (unsigned long)envp_addr == 0xffffffffUL;
}

static int ag_rt_fenv_mask(int excepts) {
  return excepts & AG_RT_FE_ALL_EXCEPT;
}

static int ag_rt_fenv_round_ok(int round) {
  return round == AG_RT_FE_TONEAREST || round == AG_RT_FE_UPWARD ||
         round == AG_RT_FE_DOWNWARD || round == AG_RT_FE_TOWARDZERO;
}

int __agc_runtime_feclearexcept(int excepts) {
  ag_rt_except_flags &= ~ag_rt_fenv_mask(excepts);
  return 0;
}

int __agc_runtime_fegetexceptflag(long flagp_addr, int excepts) {
  unsigned long long *flagp = (unsigned long long *)ag_rt_ptr(flagp_addr);
  if (flagp) *flagp = (unsigned long long)(ag_rt_except_flags & ag_rt_fenv_mask(excepts));
  return 0;
}

int __agc_runtime_feraiseexcept(int excepts) {
  ag_rt_except_flags |= ag_rt_fenv_mask(excepts);
  return 0;
}

int __agc_runtime_fesetexceptflag(long flagp_addr, int excepts) {
  unsigned long long *flagp = (unsigned long long *)ag_rt_ptr(flagp_addr);
  int mask = ag_rt_fenv_mask(excepts);
  int flags = flagp ? (int)(*flagp) : 0;
  ag_rt_except_flags = (ag_rt_except_flags & ~mask) | (flags & mask);
  return 0;
}

int __agc_runtime_fetestexcept(int excepts) {
  return ag_rt_except_flags & ag_rt_fenv_mask(excepts);
}

int __agc_runtime_fegetround(void) {
  return ag_rt_round_mode;
}

int __agc_runtime_fesetround(int round) {
  if (!ag_rt_fenv_round_ok(round)) return 1;
  ag_rt_round_mode = round;
  return 0;
}

int __agc_runtime_fegetenv(long envp_addr) {
  unsigned long long *envp = (unsigned long long *)ag_rt_ptr(envp_addr);
  if (envp) {
    envp[0] = (unsigned long long)ag_rt_round_mode;
    envp[1] = (unsigned long long)ag_rt_except_flags;
  }
  return 0;
}

int __agc_runtime_feholdexcept(long envp_addr) {
  int r = __agc_runtime_fegetenv(envp_addr);
  if (r == 0) ag_rt_except_flags = 0;
  return r;
}

int __agc_runtime_fesetenv(long envp_addr) {
  unsigned long long *envp;
  if (ag_rt_fenv_is_dfl_env(envp_addr)) {
    ag_rt_round_mode = 0;
    ag_rt_except_flags = 0;
    return 0;
  }
  envp = (unsigned long long *)ag_rt_ptr(envp_addr);
  if (envp) {
    if (!ag_rt_fenv_round_ok((int)envp[0])) return 1;
    ag_rt_round_mode = (int)envp[0];
    ag_rt_except_flags = (int)envp[1] & AG_RT_FE_ALL_EXCEPT;
  }
  return 0;
}

int __agc_runtime_feupdateenv(long envp_addr) {
  int raised = ag_rt_except_flags;
  int r = __agc_runtime_fesetenv(envp_addr);
  if (r == 0) ag_rt_except_flags |= raised;
  return r;
}

long __agc_runtime_setlocale(int category, long locale_addr) {
  if (category < 0 || category > 5) return 0;
  if (!locale_addr) return (long)ag_rt_locale_c;
  char *locale = ag_rt_ptr(locale_addr);
  if (locale[0] == 0) return (long)ag_rt_locale_c;
  if (locale[0] == 'C' && locale[1] == 0) return (long)ag_rt_locale_c;
  return 0;
}

long __agc_runtime_localeconv(void) {
  return (long)&ag_rt_lconv_value;
}
