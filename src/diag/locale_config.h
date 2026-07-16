#ifndef DIAG_LOCALE_CONFIG_H
#define DIAG_LOCALE_CONFIG_H

#if (defined(DIAG_LANG_ALL) + defined(DIAG_LANG_EN) + defined(DIAG_LANG_JA)) > 1
#error "only one diagnostic language mode may be selected"
#endif

#if defined(DIAG_LANG_EN)
#define AGC_DIAG_LOCALE_EN_ONLY 1
#elif defined(DIAG_LANG_JA)
#define AGC_DIAG_LOCALE_JA_ONLY 1
#else
#define AGC_DIAG_LOCALE_ALL 1
#endif

#endif
