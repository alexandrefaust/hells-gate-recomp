#include "thread_cpu.h"

#include <fmt/format.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace renderer {

#if defined(_WIN32)

namespace {

using GetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);

std::string ThreadName(HANDLE thread, DWORD id) {
  static auto get_description = reinterpret_cast<GetThreadDescriptionFn>(
      GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription"));
  std::string name;
  PWSTR wide = nullptr;
  if (get_description && SUCCEEDED(get_description(thread, &wide)) && wide) {
    int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (size > 1) {
      name.resize(size - 1);
      WideCharToMultiByte(CP_UTF8, 0, wide, -1, name.data(), size, nullptr, nullptr);
    }
    LocalFree(wide);
  }
  if (name.empty()) name = fmt::format("tid {}", id);
  return name;
}

struct Sample {
  uint64_t cpu_100ns = 0;
  std::string name;
};

std::unordered_map<DWORD, Sample> g_previous;

}  // namespace

std::string BusiestThreads(double window_seconds) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return {};
  const DWORD pid = GetCurrentProcessId();
  std::unordered_map<DWORD, Sample> current;
  std::vector<std::pair<double, std::string>> busy;
  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID != pid) continue;
    HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
    if (!thread) continue;
    FILETIME created, exited, kernel, user;
    if (GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
      Sample& s = current[entry.th32ThreadID];
      s.cpu_100ns = (uint64_t(kernel.dwHighDateTime) << 32 | kernel.dwLowDateTime) +
                    (uint64_t(user.dwHighDateTime) << 32 | user.dwLowDateTime);
      auto previous = g_previous.find(entry.th32ThreadID);
      if (previous != g_previous.end()) {
        s.name = previous->second.name;
        double used = double(s.cpu_100ns - previous->second.cpu_100ns) * 1e-7;
        busy.emplace_back(used / window_seconds * 100.0, s.name);
      } else {
        s.name = ThreadName(thread, entry.th32ThreadID);
      }
    }
    CloseHandle(thread);
  }
  CloseHandle(snapshot);
  g_previous = std::move(current);
  if (busy.empty()) return {};
  double total = 0.0;
  for (const auto& b : busy) total += b.first;
  std::sort(busy.begin(), busy.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  std::string out = fmt::format("CPU {:.0f}% of one core:", total);
  for (size_t i = 0; i < std::min<size_t>(busy.size(), 8) && busy[i].first >= 1.0; ++i) {
    out += fmt::format(" {} {:.0f}%,", busy[i].second, busy[i].first);
  }
  if (out.back() == ',') out.pop_back();
  return out;
}

#else

std::string BusiestThreads(double) { return {}; }

#endif

}  // namespace renderer
