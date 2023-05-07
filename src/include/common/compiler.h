#ifndef COMMON_COMPILER_H_
#define COMMON_COMPILER_H_


#if defined(__GNUC__) || defined(__clang__)
#  define BRANCH_UNLIKELY(x)     (__builtin_expect(!!(x),0))
#  define BRANCH_LIKELY(x)       (__builtin_expect(!!(x),1))
#elif (defined(__cplusplus) && (__cplusplus >= 202002L)) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
#  define BRANCH_UNLIKELY(x)     (x) [[unlikely]]
#  define BRANCH_LIKELY(x)       (x) [[likely]]
#else
#  define BRANCH_UNLIKELY(x)     (x)
#  define BRANCH_LIKELY(x)       (x)
#endif


#endif /* COMMON_COMPILER_H_ */
