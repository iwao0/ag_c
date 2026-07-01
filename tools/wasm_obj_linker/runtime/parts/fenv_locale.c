int __agc_runtime_feclearexcept(int excepts) {
  (void)excepts;
  return 0;
}

int __agc_runtime_fegetexceptflag(long flagp_addr, int excepts) {
  unsigned long long *flagp = (unsigned long long *)ag_rt_ptr(flagp_addr);
  if (flagp) *flagp = (unsigned long long)excepts;
  return 0;
}

int __agc_runtime_feraiseexcept(int excepts) {
  (void)excepts;
  return 0;
}

int __agc_runtime_fesetexceptflag(long flagp_addr, int excepts) {
  (void)flagp_addr;
  (void)excepts;
  return 0;
}

int __agc_runtime_fetestexcept(int excepts) {
  return excepts;
}

int __agc_runtime_fegetround(void) {
  return ag_rt_round_mode;
}

int __agc_runtime_fesetround(int round) {
  ag_rt_round_mode = round;
  return 0;
}

int __agc_runtime_fegetenv(long envp_addr) {
  unsigned long long *envp = (unsigned long long *)ag_rt_ptr(envp_addr);
  if (envp) {
    envp[0] = (unsigned long long)ag_rt_round_mode;
    envp[1] = 0;
  }
  return 0;
}

int __agc_runtime_feholdexcept(long envp_addr) {
  return __agc_runtime_fegetenv(envp_addr);
}

int __agc_runtime_fesetenv(long envp_addr) {
  unsigned long long *envp = (unsigned long long *)ag_rt_ptr(envp_addr);
  if (envp) ag_rt_round_mode = (int)envp[0];
  return 0;
}

int __agc_runtime_feupdateenv(long envp_addr) {
  return __agc_runtime_fesetenv(envp_addr);
}

long __agc_runtime_setlocale(int category, long locale_addr) {
  (void)category;
  (void)locale_addr;
  return (long)ag_rt_locale_c;
}

long __agc_runtime_localeconv(void) {
  return (long)&ag_rt_lconv_value;
}
