#pragma once
#include <cstdint>

uint64_t counter_get();
void counter_set(uint64_t value);
uint64_t counter_add(int64_t delta);
