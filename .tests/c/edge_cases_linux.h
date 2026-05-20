#ifndef EDGE_CASES_LINUX_H
#define EDGE_CASES_LINUX_H

/* Complex nested includes and conditionals */
#if defined(__KERNEL__) && !defined(MODULE)
# include <linux/types.h>
#else
# include <linux/types.h>
#endif

#ifdef CONFIG_X86_64
# define ARCH_REG_T uint64_t
#else
# define ARCH_REG_T uint32_t
#endif

/**
 * Macro with multi-line definition
 */
#define DECLARE_VAR(name, type) \
    type name##_var; \
    void set_##name(type val) { \
        name##_var = val; \
    }

/* Unclosed comment tests? No, that would break compilation. */

#endif
