// Copyright (c) 2004-2021  Sundog Software, LLC. All rights reserved worldwide.

#pragma once

#define SAMPLE_ASSERT_ENABLE_ASSERTS 1

#if SAMPLE_ASSERT_ENABLE_ASSERTS
#undef NDEBUG
#include <cassert>

#define ASSERT_PREDICATE assert
#else
#define ASSERT_PREDICATE(exp) static_cast<void>(0)
#endif
