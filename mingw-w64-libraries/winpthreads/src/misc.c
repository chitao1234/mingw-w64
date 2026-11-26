/*
   Copyright (c) 2011-2016  mingw-w64 project

   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* public header files */
#include "pthread.h"
/* internal header files */
#include "misc.h"

/* Keep the complete native layouts: NT 3.1 requires exactly 44 and 24 bytes,
 * respectively, on i386. Some SDKs expose only their reserved fields. */
typedef struct {
    ULONG Reserved;
    ULONG MaximumIncrement;
    ULONG PhysicalPageSize;
    ULONG NumberOfPhysicalPages;
    ULONG LowestPhysicalPage;
    ULONG HighestPhysicalPage;
    ULONG AllocationGranularity;
    ULONG_PTR LowestUserAddress;
    ULONG_PTR HighestUserAddress;
    ULONG_PTR ActiveProcessors;
    CCHAR NumberOfProcessors;
} pthread_system_basic_information;

typedef struct {
    LONG ExitStatus;
    PVOID PebBaseAddress;
    ULONG_PTR AffinityMask;
    LONG BasePriority;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR InheritedFromUniqueProcessId;
} pthread_process_basic_information;

static LONG (NTAPI *query_system_time) (PLARGE_INTEGER);
static LONG (NTAPI *query_system_information) (ULONG, PVOID, ULONG, PULONG);
static LONG (NTAPI *query_information_process) (HANDLE, ULONG, PVOID, ULONG, PULONG);
static BOOL (WINAPI *get_system_time_adjustment) (PDWORD, PDWORD, PBOOL);

static void WINAPI
fallback_get_system_time_as_file_time (LPFILETIME ft)
{
    LARGE_INTEGER time;
    SYSTEMTIME st;

    if (query_system_time && query_system_time (&time) >= 0)
    {
        ft->dwLowDateTime = time.LowPart;
        ft->dwHighDateTime = time.HighPart;
        return;
    }

    GetSystemTime (&st);
    SystemTimeToFileTime (&st, ft);
}

static BOOL WINAPI
fallback_try_enter_critical_section (LPCRITICAL_SECTION cs)
{
    HANDLE self = (HANDLE) (ULONG_PTR) GetCurrentThreadId ();

    /* NT 3.x uses the original critical-section lock count, initialized to -1.
     * Only use this implementation when the Win32 try operation is absent. */
    if (InterlockedCompareExchange (&cs->LockCount, 0, -1) == -1)
    {
        cs->OwningThread = self;
        cs->RecursionCount = 1;
        return TRUE;
    }
    if (cs->OwningThread == self)
    {
        InterlockedIncrement (&cs->LockCount);
        ++cs->RecursionCount;
        return TRUE;
    }
    return FALSE;
}

static BOOL WINAPI
fallback_get_process_affinity_mask (HANDLE process, PDWORD_PTR process_mask,
                                    PDWORD_PTR system_mask)
{
    pthread_process_basic_information process_info;
    pthread_system_basic_information system_info;

    /* ProcessBasicInformation and SystemBasicInformation both have class 0. */
    if (!query_information_process || !query_system_information
        || query_information_process (process, 0, &process_info,
                                      sizeof (process_info), NULL) < 0
        || query_system_information (0, &system_info,
                                     sizeof (system_info), NULL) < 0)
        return FALSE;

    *process_mask = process_info.AffinityMask;
    *system_mask = system_info.ActiveProcessors;
    return TRUE;
}

void (WINAPI *_pthread_get_system_time_as_file_time) (LPFILETIME) =
    fallback_get_system_time_as_file_time;
void (WINAPI *_pthread_get_system_time_best_as_file_time) (LPFILETIME) =
    fallback_get_system_time_as_file_time;
BOOL (WINAPI *_pthread_try_enter_critical_section) (LPCRITICAL_SECTION) =
    fallback_try_enter_critical_section;
BOOL (WINAPI *_pthread_get_process_affinity_mask) (HANDLE, PDWORD_PTR, PDWORD_PTR) =
    fallback_get_process_affinity_mask;
BOOL (WINAPI *_pthread_set_process_affinity_mask) (HANDLE, DWORD_PTR) = NULL;
static ULONGLONG (WINAPI *_pthread_get_tick_count_64) (VOID);
HRESULT (WINAPI *_pthread_set_thread_description) (HANDLE, PCWSTR) = NULL;
BOOL (WINAPI *_pthread_get_handle_information) (HANDLE, LPDWORD) = NULL;

#if defined(__GNUC__) || defined(__clang__)
#if __GNUC__ >= 9 && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wprio-ctor-dtor"
#endif
__attribute__((constructor(0)))
#endif
static void winpthreads_init(void)
{
    void (WINAPI *get_time) (LPFILETIME);
    BOOL (WINAPI *try_enter) (LPCRITICAL_SECTION);
    BOOL (WINAPI *get_affinity) (HANDLE, PDWORD_PTR, PDWORD_PTR);
    HMODULE mod = GetModuleHandleA("kernel32.dll");
    if (mod)
    {
        _pthread_get_handle_information =
            (BOOL (WINAPI *)(HANDLE, LPDWORD))(void*) GetProcAddress(mod, "GetHandleInformation");

        _pthread_get_tick_count_64 =
            (ULONGLONG (WINAPI *)(VOID))(void*) GetProcAddress(mod, "GetTickCount64");

        _pthread_set_thread_description =
            (HRESULT (WINAPI *)(HANDLE, PCWSTR))(void*) GetProcAddress(mod, "SetThreadDescription");

        get_time = (void (WINAPI *)(LPFILETIME))(void*)
            GetProcAddress (mod, "GetSystemTimeAsFileTime");
        if (get_time)
            _pthread_get_system_time_as_file_time = get_time;

        get_system_time_adjustment = (BOOL (WINAPI *)(PDWORD, PDWORD, PBOOL))(void*)
            GetProcAddress (mod, "GetSystemTimeAdjustment");

        try_enter = (BOOL (WINAPI *)(LPCRITICAL_SECTION))(void*)
            GetProcAddress (mod, "TryEnterCriticalSection");
        if (try_enter)
            _pthread_try_enter_critical_section = try_enter;

        get_affinity = (BOOL (WINAPI *)(HANDLE, PDWORD_PTR, PDWORD_PTR))(void*)
            GetProcAddress (mod, "GetProcessAffinityMask");
        if (get_affinity)
            _pthread_get_process_affinity_mask = get_affinity;

        /* NT 3.x has neither this export nor the native process affinity
         * information class used to implement it on NT 4 and later. */
        _pthread_set_process_affinity_mask = (BOOL (WINAPI *)(HANDLE, DWORD_PTR))(void*)
            GetProcAddress (mod, "SetProcessAffinityMask");

        /* <1us precision on Windows 10 */
        get_time = (void (WINAPI *)(LPFILETIME))(void*)
            GetProcAddress (mod, "GetSystemTimePreciseAsFileTime");
        _pthread_get_system_time_best_as_file_time = get_time ? get_time :
            _pthread_get_system_time_as_file_time;
    }

    mod = GetModuleHandleA ("ntdll.dll");
    if (mod)
    {
        query_system_time = (LONG (NTAPI *)(PLARGE_INTEGER))(void*)
            GetProcAddress (mod, "NtQuerySystemTime");
        query_system_information = (LONG (NTAPI *)(ULONG, PVOID, ULONG, PULONG))(void*)
            GetProcAddress (mod, "NtQuerySystemInformation");
        query_information_process = (LONG (NTAPI *)(HANDLE, ULONG, PVOID, ULONG, PULONG))(void*)
            GetProcAddress (mod, "NtQueryInformationProcess");
    }

    /* Although SetThreadDescription lives in kernel32.dll, on Windows Server 2016,
     * Windows 10 LTSB 2016 and Windows 10 version 1607, it was only available in
     * kernelbase.dll. So, load it from there for maximum coverage.
     */
    if (!_pthread_set_thread_description)
    {
        mod = GetModuleHandleA("kernelbase.dll");
        if (mod)
        {
            _pthread_set_thread_description =
                (HRESULT (WINAPI *)(HANDLE, PCWSTR))(void*) GetProcAddress(mod, "SetThreadDescription");
        }
    }
}
#if defined(__GNUC__) && __GNUC__ >= 9 && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#if defined(_MSC_VER) && !defined(__clang__)
/* Force a reference to __xc_t to prevent whole program optimization
 * from discarding the variable. */

/* On x86, symbols are prefixed with an underscore. */
# if defined(_M_IX86)
#   pragma comment(linker, "/include:___xc_t")
# else
#   pragma comment(linker, "/include:__xc_t")
# endif

#pragma section(".CRT$XCT", long, read)
__declspec(allocate(".CRT$XCT"))
extern const _PVFV __xc_t;
const _PVFV __xc_t = winpthreads_init;
#endif

BOOL
_pthread_get_time_increment (PDWORD increment)
{
    DWORD adjustment, value;
    BOOL disabled;
    pthread_system_basic_information info;

    if (get_system_time_adjustment
        && get_system_time_adjustment (&adjustment, &value, &disabled) && value)
    {
        *increment = value;
        return TRUE;
    }
    if (query_system_information
        && query_system_information (0, &info, sizeof (info), NULL) >= 0
        && info.MaximumIncrement)
    {
        *increment = info.MaximumIncrement;
        return TRUE;
    }
    return FALSE;
}

unsigned long long _pthread_time_in_ms(void)
{
    FILETIME ft;

    _pthread_get_system_time_as_file_time (&ft);
    return (((unsigned long long)ft.dwHighDateTime << 32) + ft.dwLowDateTime
            - 0x19DB1DED53E8000ULL) / 10000ULL;
}

unsigned long long _pthread_time_in_ms_from_timespec(const struct _timespec64 *ts)
{
    unsigned long long t = (unsigned long long) ts->tv_sec * 1000LL;
    /* The +999999 is here to ensure that the division always rounds up */
    t += (unsigned long long) (ts->tv_nsec + 999999) / 1000000;

    return t;
}

unsigned long long _pthread_rel_time_in_ms(const struct _timespec64 *ts)
{
    unsigned long long t1 = _pthread_time_in_ms_from_timespec(ts);
    unsigned long long t2 = _pthread_time_in_ms();

    /* Prevent underflow */
    if (t1 < t2) return 0;
    return t1 - t2;
}

static unsigned long long
_pthread_get_tick_count (long long *frequency)
{
  if (_pthread_get_tick_count_64 != NULL)
    return _pthread_get_tick_count_64 ();

  LARGE_INTEGER freq, timestamp;

  if (*frequency == 0)
  {
    if (QueryPerformanceFrequency (&freq) && freq.QuadPart >= 1000)
      *frequency = freq.QuadPart;
    else
      *frequency = -1;
  }

  if (*frequency > 0 && QueryPerformanceCounter (&timestamp))
    return timestamp.QuadPart / (*frequency / 1000);

  /* Keep using the fallback for the rest of this wait. */
  *frequency = -1;
  return GetTickCount ();
}

static unsigned long long
_pthread_get_elapsed_time (unsigned long long start_time, long long *frequency)
{
  long long previous_frequency = *frequency;
  unsigned long long current_time = _pthread_get_tick_count (frequency);

  /* If QPC fails during a wait, accept the native timeout rather than
   * comparing timestamps from different clocks. */
  if (*frequency != previous_frequency)
    return ~0ULL;

  /* GetTickCount wraps at 2^32 ms; every finite wait is shorter than that. */
  if (*frequency < 0)
    return (DWORD) (current_time - start_time);
  return current_time - start_time;
}

/* A wrapper around WaitForSingleObject() that ensures that
 * the wait function does not time out before the time
 * actually runs out. This is needed because WaitForSingleObject()
 * might have poor accuracy, returning earlier than expected.
 * On the other hand, returning a bit *later* than expected
 * is acceptable in a preemptive multitasking environment.
 */
unsigned long
_pthread_wait_for_single_object (void *handle, unsigned long timeout)
{
  DWORD result;
  unsigned long long start_time;
  unsigned long wait_time;
  long long frequency = 0;

  if (timeout == INFINITE || timeout == 0)
    return WaitForSingleObject ((HANDLE) handle, (DWORD) timeout);

  start_time = _pthread_get_tick_count (&frequency);
  wait_time = timeout;

  do
  {
    unsigned long long elapsed_time;

    result = WaitForSingleObject ((HANDLE) handle, (DWORD) wait_time);
    if (result != WAIT_TIMEOUT)
      break;

    elapsed_time = _pthread_get_elapsed_time (start_time, &frequency);
    if (elapsed_time >= timeout)
      break;

    wait_time = timeout - (DWORD) elapsed_time;
  } while (TRUE);

  return result;
}

/* A wrapper around WaitForMultipleObjects() that ensures that
 * the wait function does not time out before the time
 * actually runs out. This is needed because WaitForMultipleObjects()
 * might have poor accuracy, returning earlier than expected.
 * On the other hand, returning a bit *later* than expected
 * is acceptable in a preemptive multitasking environment.
 */
unsigned long
_pthread_wait_for_multiple_objects (unsigned long count, void **handles, unsigned int all, unsigned long timeout)
{
  DWORD result;
  unsigned long long start_time;
  unsigned long wait_time;
  long long frequency = 0;

  if (timeout == INFINITE || timeout == 0)
    return WaitForMultipleObjects ((DWORD) count, (HANDLE *) handles, all, (DWORD) timeout);

  start_time = _pthread_get_tick_count (&frequency);
  wait_time = timeout;

  do
  {
    unsigned long long elapsed_time;

    result = WaitForMultipleObjects ((DWORD) count, (HANDLE *) handles, all, (DWORD) wait_time);
    if (result != WAIT_TIMEOUT)
      break;

    elapsed_time = _pthread_get_elapsed_time (start_time, &frequency);
    if (elapsed_time >= timeout)
      break;

    wait_time = timeout - (DWORD) elapsed_time;
  } while (TRUE);

  return result;
}
