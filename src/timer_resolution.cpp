#include "timer_resolution.h"

#include <rex/cvar.h>
#include <rex/logging/macros.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif

REXCVAR_DEFINE_BOOL(high_resolution_timer, true, "System",
                    "Request 1 ms Windows timer resolution, so short waits in the game take as "
                    "long as asked (frame pacing, audio)");

namespace timer_resolution {

void Apply() {
#ifdef _WIN32
  bool requested = REXCVAR_GET(high_resolution_timer) && timeBeginPeriod(1) == TIMERR_NOERROR;
  // Current resolution in 100 ns units (ntdll), for logs from slow systems.
  using QueryFn = LONG(WINAPI*)(ULONG*, ULONG*, ULONG*);
  auto query = reinterpret_cast<QueryFn>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryTimerResolution"));
  ULONG coarsest = 0, finest = 0, current = 0;
  if (query && query(&coarsest, &finest, &current) == 0) {
    REXLOG_INFO("SESSION: timer resolution {:.2f} ms ({})", current / 10000.0,
                requested ? "1 ms requested" : "default");
  }
#endif
}

}  // namespace timer_resolution
