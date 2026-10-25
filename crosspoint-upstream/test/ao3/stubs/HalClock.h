#pragma once
struct FakeClock {
  void syncFromNTP() {}
};
inline FakeClock halClock;
