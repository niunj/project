UNAME := $(shell uname)

PROJECT = OpenGL32Example

ifeq ($(UNAME), Darwin)

CCC = clang++

CCFLAGS = -I"../../Public Headers" -I/usr/X11R6/include -DMAC -O3

LDFLAGS = -L"../../lib/macos" -L/usr/X11R6/lib

LIBS = -lSilverLiningOpenGL32Core -framework GLUT -framework OpenGL

else

CCC = g++

CCFLAGS = -I"../../Public Headers" -DLINUX -DGLUT_DISABLE_ATEXIT_HACK -O3

LDFLAGS = -L"../../lib/linux"

LIBS = -lSilverLiningOpenGL32Core -lglut -lpthread -lGLU -lGL -ldl

endif

SRC = OpenGL32Example.cpp

OBJ = OpenGL32Example.o

all: $(PROJECT)

$(PROJECT): $(OBJ)
	$(CCC) $(CCFLAGS) $(OBJ) -o $@ $(LDFLAGS) $(LIBS)

OpenGL32Example.o : OpenGL32Example.cpp
	$(CCC) $(CCFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(PROJECT) Makefile.bak
