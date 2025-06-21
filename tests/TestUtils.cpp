#include "TestUtils.h"

uint32_t fakeTime = 0;
uint32_t getTimeMock() { return fakeTime; }
void sleepMock(uint32_t ms) { fakeTime += ms; }

void realSleep(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
