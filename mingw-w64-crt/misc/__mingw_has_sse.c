/**
 * This file has no copyright assigned and is placed in the Public Domain.
 * This file is part of the mingw-w64 runtime package.
 * No warranty is given; refer to the file DISCLAIMER.PD within this package.
 */

#if defined(__i386__)
#include <windows.h>

int __mingw_has_sse (void);
static int __has_sse = -1;

static int has_sse_cpuid(void)
{
  int cpuInfo[4], infoType = 1;
  int o_flag, n_flag;

  __asm__ volatile ("pushfl\n\tpopl %0" : "=mr" (o_flag));
  n_flag = o_flag ^ 0x200000;
  __asm__ volatile ("pushl %0\n\tpopfl" : : "g" (n_flag));
  __asm__ volatile ("pushfl\n\tpopl %0" : "=mr" (n_flag));
  if (n_flag == o_flag)
    return 0;

  __asm__ __volatile__ (
    "cpuid"
    : "=a" (cpuInfo[0]), "=b" (cpuInfo[1]), "=c" (cpuInfo[2]),
    "=d" (cpuInfo[3])
    : "a" (infoType));
  return (cpuInfo[3] & 0x2000000) != 0;
}

int __mingw_has_sse(void)
{
  HMODULE module;
  BOOL (WINAPI *func)(DWORD);

  if (__has_sse != -1)
    return __has_sse;

  if (!has_sse_cpuid())
    {
      __has_sse = 0;
      return 0;
    }

  module = GetModuleHandleA("kernel32.dll");
  func = module ? (LPVOID)GetProcAddress(module, "IsProcessorFeaturePresent") : (LPVOID)NULL;
  if (func)
    {
      /* Old NT can run on CPUs with SSE, but user mode still faults on STMXCSR
       * unless the OS exposes XMM state support.
       */
      __has_sse = func(PF_XMMI_INSTRUCTIONS_AVAILABLE) ? 1 : 0;
      return __has_sse;
    }

  __has_sse = 0;
  return 0;
}
#endif  /* __i386__ */
