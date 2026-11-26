/*
 * This file has no copyright assigned and is placed in the Public Domain.
 * This file is part of the mingw-w64 runtime package.
 * No warranty is given; refer to the file DISCLAIMER.PD within this package.
 */

/* Exercise the wait implementation with injected clocks and native waits. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static ULONGLONG timestamps[4];
static DWORD expected_waits[3];
static unsigned int timestamp_count, timestamp_index, wait_count, wait_index;
static unsigned int frequency_calls, counter_calls, counter_failure;
static LONGLONG counter_frequency;
static DWORD native_result;
static const char *test_name;

#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf (stderr, "%s:%d: %s\n", test_name, __LINE__, #condition); \
    exit (1); \
  } \
} while (0)

static ULONGLONG
next_timestamp (void)
{
  CHECK (timestamp_index < timestamp_count);
  return timestamps[timestamp_index++];
}

static ULONGLONG WINAPI
mock_tick_count_64 (void)
{
  return next_timestamp ();
}

static DWORD WINAPI
mock_tick_count (void)
{
  return (DWORD) next_timestamp ();
}

static BOOL WINAPI
mock_frequency (LARGE_INTEGER *frequency)
{
  ++frequency_calls;
  frequency->QuadPart = counter_frequency;
  return counter_frequency != 0;
}

static BOOL WINAPI
mock_counter (LARGE_INTEGER *counter)
{
  if (++counter_calls == counter_failure)
    return FALSE;
  counter->QuadPart = next_timestamp ();
  return TRUE;
}

static DWORD WINAPI
mock_single_wait (HANDLE handle, DWORD timeout)
{
  (void) handle;
  CHECK (wait_index < wait_count);
  CHECK (timeout == expected_waits[wait_index]);
  ++wait_index;
  return native_result;
}

static DWORD WINAPI
mock_multiple_wait (DWORD count, const HANDLE *handles, BOOL all, DWORD timeout)
{
  CHECK (count == 1);
  CHECK (all == FALSE);
  return mock_single_wait (handles[0], timeout);
}

/* Include the implementation so the test can exercise its static clock
 * selection without adding clock injection hooks to the library ABI. */
#define GetTickCount mock_tick_count
#define QueryPerformanceFrequency mock_frequency
#define QueryPerformanceCounter mock_counter
#define WaitForSingleObject mock_single_wait
#define WaitForMultipleObjects mock_multiple_wait
#include "../../src/misc.c"

enum clock_source { TICK_COUNT, TICK_COUNT_64, PERFORMANCE_COUNTER };

static void
reset (const char *name, enum clock_source source)
{
  test_name = name;
  timestamp_count = timestamp_index = wait_count = wait_index = 0;
  frequency_calls = counter_calls = counter_failure = 0;
  counter_frequency = source == PERFORMANCE_COUNTER ? 1000 : 0;
  native_result = WAIT_TIMEOUT;
  _pthread_get_tick_count_64 = source == TICK_COUNT_64 ? mock_tick_count_64 : NULL;
}

static void
run_wait (int multiple, DWORD timeout, DWORD expected_result)
{
  HANDLE handle = NULL;
  DWORD result;
  if (multiple)
    result = _pthread_wait_for_multiple_objects (1, &handle, FALSE, timeout);
  else
    result = _pthread_wait_for_single_object (handle, timeout);
  CHECK (result == expected_result);
  CHECK (wait_index == wait_count);
  CHECK (timestamp_index == timestamp_count);
}

static void
expired (int multiple, const char *name, enum clock_source source,
         ULONGLONG start, ULONGLONG finish, DWORD timeout)
{
  reset (name, source);
  timestamps[0] = start;
  timestamps[1] = finish;
  timestamp_count = 2;
  expected_waits[0] = timeout;
  wait_count = 1;
  run_wait (multiple, timeout, WAIT_TIMEOUT);
}

static void
test_wait (int multiple)
{
  enum clock_source source;
  DWORD results[] = { WAIT_OBJECT_0, WAIT_ABANDONED_0, WAIT_FAILED };
  unsigned int i;

  expired (multiple, "ordinary timeout", TICK_COUNT, 1000, 1100, 100);
  expired (multiple, "wrap at deadline", TICK_COUNT, 0xfffffff0, 0x54, 100);
  expired (multiple, "wrap one ms late", TICK_COUNT, 0xfffffff0, 0x55, 100);
  expired (multiple, "wrap 100 ms late", TICK_COUNT, 0xfffffff0, 0xb8, 100);
  expired (multiple, "largest finite timeout", TICK_COUNT, 100, 98, INFINITE - 1);
  expired (multiple, "largest timeout late", TICK_COUNT, 100, 99, INFINITE - 1);

  reset ("early timeout across wrap", TICK_COUNT);
  timestamps[0] = 0xfffffff0;
  timestamps[1] = 0x20;
  timestamps[2] = 0x54;
  timestamp_count = 3;
  expected_waits[0] = 100;
  expected_waits[1] = 52;
  wait_count = 2;
  run_wait (multiple, 100, WAIT_TIMEOUT);

  for (source = TICK_COUNT; source <= PERFORMANCE_COUNTER; ++source)
    {
      reset ("early timeout", source);
      timestamps[0] = 1000;
      timestamps[1] = 1040;
      timestamps[2] = 1100;
      timestamp_count = 3;
      expected_waits[0] = 100;
      expected_waits[1] = 60;
      wait_count = 2;
      run_wait (multiple, 100, WAIT_TIMEOUT);

      reset ("zero timeout", source);
      expected_waits[0] = 0;
      wait_count = 1;
      run_wait (multiple, 0, WAIT_TIMEOUT);
      CHECK (frequency_calls == 0 && counter_calls == 0);

      reset ("infinite wait", source);
      native_result = WAIT_OBJECT_0;
      expected_waits[0] = INFINITE;
      wait_count = 1;
      run_wait (multiple, INFINITE, WAIT_OBJECT_0);
      CHECK (frequency_calls == 0 && counter_calls == 0);

      for (i = 0; i < sizeof (results) / sizeof (results[0]); ++i)
        {
          reset ("native non-timeout result", source);
          native_result = results[i];
          timestamps[0] = 1000;
          timestamp_count = 1;
          expected_waits[0] = 100;
          wait_count = 1;
          run_wait (multiple, 100, results[i]);
        }
    }

  for (source = TICK_COUNT_64; source <= PERFORMANCE_COUNTER; ++source)
    {
      expired (multiple, "64-bit low word wraps", source,
               0xfffffff0ULL, 0x100000055ULL, 100);
      expired (multiple, "64-bit elapsed exceeds a cycle", source,
               1000, 0x1000003e8ULL, 100);
    }

  reset ("QPC fails on first sample", PERFORMANCE_COUNTER);
  counter_failure = 1;
  timestamps[0] = 0xfffffff0;
  timestamps[1] = 0x54;
  timestamp_count = 2;
  expected_waits[0] = 100;
  wait_count = 1;
  run_wait (multiple, 100, WAIT_TIMEOUT);
  CHECK (counter_calls == 1);

  reset ("QPC fails during wait", PERFORMANCE_COUNTER);
  counter_failure = 2;
  timestamps[0] = 0x1000003e8ULL;
  timestamps[1] = 55;
  timestamp_count = 2;
  expected_waits[0] = 100;
  wait_count = 1;
  run_wait (multiple, 100, WAIT_TIMEOUT);

  reset ("low frequency uses tick count", PERFORMANCE_COUNTER);
  counter_frequency = 100;
  timestamps[0] = 0xfffffff0;
  timestamps[1] = 0x54;
  timestamp_count = 2;
  expected_waits[0] = 100;
  wait_count = 1;
  run_wait (multiple, 100, WAIT_TIMEOUT);
  CHECK (counter_calls == 0);
}

int
main (void)
{
  test_wait (0);
  test_wait (1);
  puts ("wait timeout regression tests passed");
  return 0;
}
