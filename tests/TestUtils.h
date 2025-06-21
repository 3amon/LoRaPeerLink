#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include "MockRadio.h"
#include <thread>
#include <chrono>
#include <cstdint>

extern uint32_t fakeTime;
extern uint32_t getTimeMock();
extern void sleepMock(uint32_t ms);
extern void realSleep(uint32_t ms);

#endif // TEST_UTILS_H