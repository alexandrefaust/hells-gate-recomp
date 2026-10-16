// Windows timer resolution. Guest sleeps and timed waits become ::Sleep and
// WaitForSingleObjectEx in milliseconds; at the default 15.6 ms resolution a
// 1 ms wait takes up to 15.6 ms, which starves frame pacing and audio on
// systems where nothing else in the process raises it.

#pragma once

namespace timer_resolution {

// Requests 1 ms (high_resolution_timer) and logs the resolution in effect.
void Apply();

}  // namespace timer_resolution
