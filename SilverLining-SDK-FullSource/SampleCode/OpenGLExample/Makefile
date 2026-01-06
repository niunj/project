UNAME := $(shell uname)

PROJECT = OpenGLExample

ifeq ($(UNAME), Darwin)

CCC = clang++

CCFLAGS = -I"../../Public Headers" -I/usr/X11R6/include -DMAC -O3

LDFLAGS = -L"../../lib/macos" -L/usr/X11R6/lib

LIBS = -lSilverLiningOpenGL -framework GLUT -framework OpenGL

else

CCC = g++

CCFLAGS = -I"../../Public Headers" -DLINUX -DGLUT_DISABLE_ATEXIT_HACK -O3

LDFLAGS = -L"../../lib/linux"

LIBS = -lSilverLiningOpenGL -lglut -lpthread -lGLU -lGL -ldl

endif

SRC = OpenGLExample.cpp

OBJ = OpenGLExample.o

all: $(PROJECT)

$(PROJECT): $(OBJ)
	$(CCC) $(CCFLAGS) $(OBJ) -o $@ $(LDFLAGS) $(LIBS)

OpenGLExample.o : OpenGLExample.cpp
	$(CCC) $(CCFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(PROJECT) Makefile.bak
