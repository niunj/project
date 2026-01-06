UNAME := $(shell uname)

PROJECT = OpenGLMultiThreadedSample

ifeq ($(UNAME), Darwin)

CCC = clang++

CCFLAGS = -I"../../Public Headers" -I"." -I"third-party/glfw-3.0.4/include" -I/usr/X11R6/include -DMAC -O3

LDFLAGS = -L"../../lib/macos" -L"third-party/glfw-3.0.4/lib/macos" -L/usr/X11R6/lib

LIBS = -lSilverLiningOpenGL32Core -lglfw3 -framework GLUT -framework OpenGL

else

CCC = g++

CCFLAGS = -I"../../Public Headers" -I"." -I"third-party/glfw-3.0.4/include" -DLINUX -DGLUT_DISABLE_ATEXIT_HACK -O3

LDFLAGS = -L"../../lib/linux" -L"."

LIBS = -lSilverLiningOpenGL32Core -lglfw -lglut -lpthread -lGLU -lGL -lX11 -lrt -ldl

endif

SRC = DrawDeferredMultiThreaded.cpp DrawDeferredSingleThreaded.cpp DrawImmediateSingleThreaded.cpp DrawStrategy.cpp main.cpp MatrixUtils.cpp QuadRendering.cpp RenderToTexture.cpp SetUpLayers.cpp SampleApp.cpp glew.cpp

OBJ = DrawDeferredMultiThreaded.o DrawDeferredSingleThreaded.o  DrawImmediateSingleThreaded.o DrawStrategy.o main.o MatrixUtils.o QuadRendering.o RenderToTexture.o SetUpLayers.o SampleApp.o glew.o

all: $(PROJECT)

$(PROJECT): $(OBJ)
	$(CCC) $(CCFLAGS) $(OBJ) -o $@ $(LDFLAGS) $(LIBS)

%.o : %.cpp
	$(CCC) $(CCFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(PROJECT) Makefile.bak
