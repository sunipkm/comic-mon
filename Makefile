CC=gcc
CXX=g++
RM= /bin/rm -vf
ARCH=UNDEFINED
PWD=$(shell pwd)
CDR=$(shell pwd)
ECHO=echo

EDCFLAGS:=$(CFLAGS) -I include/ -Wall -std=gnu11
EDLDFLAGS:=$(LDFLAGS) -lpthread -lm
EDDEBUG:=$(DEBUG)

ifeq ($(ARCH),UNDEFINED)
	ARCH=$(shell uname -m)
endif

UNAME_S := $(shell uname -s)

CXXFLAGS:= -I include/ -I imgui/include -I imgui/libs/gl3w -DIMGUI_IMPL_OPENGL_LOADER_GL3W -I ./ -Wall -O2 -fpermissive -std=gnu++11
LIBS = -lpthread -ljpeg

ifeq ($(UNAME_S), Linux) #LINUX
	ECHO_MESSAGE = "Linux"
	LIBS += -lGL `pkg-config --static --libs glfw3`
	LIBEXT= so
	LINKOPTIONS:= -shared
	CXXFLAGS += `pkg-config --cflags glfw3`
endif

ifeq ($(UNAME_S), Darwin) #APPLE
	ECHO_MESSAGE = "Mac OS X"
	LIBEXT= dylib
	LINKOPTIONS:= -dynamiclib -single_module
	CXXFLAGS:= -arch $(ARCH) $(CXXFLAGS)
	LIBS += -arch $(ARCH) -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
	LIBS += -L/usr/local/lib -L/opt/local/lib
	#LIBS += -lglfw3
	LIBS += -lglfw

	CXXFLAGS += -I/usr/local/include -I/opt/local/include
	CFLGAS+= -arch $(ARCH)
endif

all: CFLAGS+= -O2

GUITARGET=client.out

CTARGET=server.out

COBJS=server.o

CPPOBJS=guimain.o

TESTJPEG=jpegtest.o

all: $(GUITARGET) $(TESTJPEG) $(CTARGET) imgui/libimgui_glfw.a
	$(CXX) $(CXXFLAGS) -o testjpeg.out $(TESTJPEG) imgui/libimgui_glfw.a $(LIBS)
	$(ECHO) "Built for $(UNAME_S), execute ./$(GUITARGET)"

comic:
	$(CXX) -o comicmon.out atikserver.cpp -O2 -latikccd -lusb-1.0 -ljpeg -lm -lpthread -lcfitsio
	sudo pigpiod
	sudo ./startcam.py

$(GUITARGET): $(CPPOBJS) imgui/libimgui_glfw.a
	$(CXX) $(CXXFLAGS) -o $@ $(CPPOBJS) imgui/libimgui_glfw.a $(LIBS)

$(CTARGET): $(COBJS) 
	$(CC) $(EDCFLAGS) -o $@ $(COBJS) $(EDLDFLAGS)

imgui/libimgui_glfw.a:
	cd $(PWD)/imgui && make -j$(nproc) && cd $(PWD)

%.o: %.c
	$(CC) $(EDCFLAGS) -o $@ -c $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

.PHONY: clean

clean:
	$(RM) $(GUITARGET)
	$(RM) $(CTARGET)
	$(RM) $(COBJS)
	$(RM) $(CPPOBJS)
	$(RM) $(TESTJPEG)
	$(RM) testjpeg.out

spotless: clean
	cd $(PWD)/imgui && make spotless && cd $(PWD)
