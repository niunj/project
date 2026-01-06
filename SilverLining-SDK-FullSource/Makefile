# Makefile for SilverLining static library.

UNAME := $(shell uname)
MACHINE := $(shell uname -m)
PROJECT = libSilverLining.a
DEBUG_BUILD = $(BUILD_TYPE)

ifeq ($(DEBUG_BUILD), Debug)
DEBUG_FLAGS = -O0 -c -g3 -ggdb3 -fno-inline
else
DEBUG_FLAGS = -O3 -c
endif

LIBSRC = $(wildcard Src/*.cpp)
OGLSRCALL = $(wildcard SilverLiningOpenGL/*.cpp) third-party/TextureLoader.cpp
OGLSRC = $(filter-out SilverLiningOpenGL/StdAfx.cpp, $(OGLSRCALL))
OGL32SRCALL = $(wildcard SilverLiningOpenGL32/*.cpp) third-party/TextureLoader.cpp
OGL32SRC = $(filter-out SilverLiningOpenGL32/StdAfx.cpp, $(OGL32SRCALL))
VULKANSRC = $(wildcard SilverLiningVulkan/*.cpp)
LIBOBJ = $(join $(dir $(LIBSRC)), $(addsuffix .o, $(notdir $(basename $(LIBSRC)))))
OGLOBJ = $(join $(dir $(OGLSRC)), $(addsuffix .o, $(notdir $(basename $(OGLSRC)))))
OGL32OBJ = $(join $(dir $(OGL32SRC)), $(addsuffix .o, $(notdir $(basename $(OGL32SRC)))))
VULKANOBJ = $(join $(dir $(VULKANSRC)), $(addsuffix .o, $(notdir $(basename $(VULKANSRC)))))
ifeq ($(UNAME), Darwin)

CCC = clang

ifeq ($(MACHINE), x86_64)
ARCH = x86_64
else
ARCH = i386
endif

CCFLAGS32CORE = -DMAC -DSILVERLINING_STATIC_RENDERER_OPENGL32 -I"Public Headers" -I"third-party" -I"Private Headers" -I"third-party/GL/include"  $(DEBUG_FLAGS) -arch $(ARCH)

CCFLAGSSTATIC = -DMAC -DSILVERLINING_STATIC_RENDERER_CUSTOM -I"Public Headers" -I"third-party" -I"Private Headers" -I"third-party/GL/include"  $(DEBUG_FLAGS) -arch $(ARCH)

OUT = lib/macos/libSilverLiningOpenGL.a

OUT32CORE = lib/macos/libSilverLiningOpenGL32Core.a

OUTSTATIC = lib/macos/libSilverLining-norenderer.a



all: oglMac staticMac

else

CCC = g++

CCFLAGS = -std=c++11 -DLINUX -DDONGLE_SUPPORT -DSILVERLINING_STATIC_RENDERER_OPENGL -I"Public Headers" -I"third-party" -I"Private Headers" -I"third-party/GL/include"  $(DEBUG_FLAGS)

CCFLAGS32CORE = -std=c++11 -DLINUX -DDONGLE_SUPPORT -DSILVERLINING_STATIC_RENDERER_OPENGL32 -I"Public Headers" -I"third-party" -I"Private Headers" -I"third-party/GL/include"  $(DEBUG_FLAGS)

CCFLAGSSTATIC = -std=c++11 -DLINUX -DDONGLE_SUPPORT -DSILVERLINING_STATIC_RENDERER_CUSTOM -I"Public Headers" -I"third-party" -I"Private Headers" -I"third-party/GL/include"  $(DEBUG_FLAGS)

CCFLAGSVULKAN = -std=c++17 -DLINUX -DDONGLE_SUPPORT -DSILVERLINING_STATIC_RENDERER_VULKAN -I"Public Headers" -I"third-party" -I"Private Headers" -I"third-party/GL/include" -I"third-party/glm" $(DEBUG_FLAGS)

OUT = lib/linux/libSilverLiningOpenGL.a

OUT32CORE = lib/linux/libSilverLiningOpenGL32Core.a

OUTSTATIC = lib/linux/libSilverLining-norenderer.a

OUTVULKAN = lib/linux/libSilverLiningVulkan.a

all: ogl ogl32core static

endif

ogl : CXXFLAGS = $(CCFLAGS)

ogl32core : CXXFLAGS = $(CCFLAGS32CORE)

static : CXXFLAGS = $(CCFLAGSSTATIC)

vulkan : CXXFLAGS = $(CCFLAGSVULKAN)

Src/%.o: Src/%.cpp
	$(CCC) $(CXXFLAGS) $< -o $@

SilverLiningOpenGL/%.o : SilverLiningOpenGL/%.cpp
	$(CCC) $(CXXFLAGS) $< -o $@

SilverLiningOpenGL32/%.o : SilverLiningOpenGL32/%.cpp
	$(CCC) $(CXXFLAGS) $< -o $@

SilverLiningVulkan/%.o : SilverLiningVulkan/%.cpp
	$(CCC) $(CXXFLAGS) $< -o $@

third-party/%.o : third-party/%.cpp
	$(CCC) $(CXXFLAGS) $< -o $@

ogl: $(OGLOBJ) $(LIBOBJ) $(TEXLOADEROBJ)
	ar rcs $(OUT) $(OGLOBJ) $(LIBOBJ)

oglMac: $(OGLOBJ) $(LIBOBJ)
	libtool -static -o $(OUT) -arch_only $(ARCH) $(OGLOBJ) $(LIBOBJ)
	#lipo -create $(OUT)-$(ARCH) -output $(OUT)

ogl32core: $(OGL32OBJ) $(LIBOBJ)
	ar rcs $(OUT32CORE) $(OGL32OBJ) $(LIBOBJ)

vulkan: $(VULKANOBJ) $(LIBOBJ)
	./Resources/Shaders/compile-spv.sh
	ar rcs $(OUTVULKAN) $(VULKANOBJ) $(LIBOBJ)

static: $(LIBOBJ)
	ar rcs $(OUTSTATIC) $(LIBOBJ)

clean:
	rm -f $(OGLOBJ) $(OUT) $(OGL32OBJ) $(OUT32CORE) $(VULKANOBJ) $(LIBOBJ) $(OUTSTATIC) Makefile.bak

cleanobj:
	rm -f $(OGLOBJ) $(OGL32OBJ) $(LIBOBJ) $(VULKANOBJ)

	

