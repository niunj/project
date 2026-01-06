// Copyright (c) 2004-2023 Sundog Software, LLC. All rights reserved worldwide.

// The following ifdef block is the standard way of creating macros which make exporting
// from a DLL simpler. All files within this DLL are compiled with the SILVERLININGVULKAN_EXPORTS
// symbol defined on the command line. this symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see
// SILVERLININGVULKAN_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.
#ifdef SILVERLININGVULKAN_EXPORTS
#define SILVERLININGDLL_API extern "C" __declspec(dllexport)
#else
//#define SILVERLININGDLL_API extern "C" __declspec(dllimport)
#define SILVERLININGDLL_API extern "C"
#endif

#ifndef SILVERLINING_STATIC_RENDERER_VULKAN
#include "../Public Headers/SilverLiningDLLCommon.h"
#else
#include "SilverLiningDLLCommon.h"
#endif
