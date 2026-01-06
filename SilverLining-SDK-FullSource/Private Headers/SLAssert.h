#pragma once

#define SL_ENABLE_ASSERTS 0

#if SL_ENABLE_ASSERTS
#undef NDEBUG
#include <cassert>

#define SL_ASSERT assert
#else
#define SL_ASSERT(exp) static_cast<void>(0)
#endif


