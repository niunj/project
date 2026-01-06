// Copyright 2006-2020 Sundog Software, LLC. All rights reserved worldwide.

/** \file DrawIndirectCommands.h
\brief structures for indirect, bindless rendering
*/

#pragma once

#include "SilverLiningTypes.h"

#ifdef SWIG
%module SilverLiningDrawIndirectCommands
#define SILVERLINING_API
%{
#include "DrawIndirectCommands.h"
	using namespace SilverLining;
	% }
#endif

namespace SilverLining
{
	typedef  struct {
		unsigned int  count;
		unsigned int  instanceCount;
		unsigned int  firstIndex;
		unsigned int  baseVertex;
		unsigned int  baseInstance;
	} DrawElementsIndirectCommand;

	struct BindlessPtrNV {
		unsigned int   index;
		unsigned int   reserved;
		GPUAddressType address;
		GPULengthType length;
	} ;

	struct DrawElementsIndirectBindlessCommandNV {
		DrawElementsIndirectCommand cmd;
		unsigned                      reserved;
		BindlessPtrNV               indexBuffer;
		BindlessPtrNV               vertexBuffers[3];
	};
}