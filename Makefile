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

CXXFLAGS:= -I include/ -I imgui/include -I imgui/include/imgui -I imgui/include/backend -I ./ -Wall -O2 -fpermissive -std=gnu++11
LIBS = -lpthread -ljpeg
ATIKCXXFLAGS = -Wall -O2 -std=gnu++11
ATIKLDFLAGS = -lpthread -lm -latikccd -lusb-1.0 -ljpeg -lcfitsio

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

CPPOBJS=guimain.o

TESTJPEG=jpegtest.o

all: $(GUITARGET) $(TESTJPEG) imgui/libimgui_glfw.a
	$(CXX) $(CXXFLAGS) -o testjpeg.out $(TESTJPEG) imgui/libimgui_glfw.a $(LIBS)
	$(ECHO) "Built for $(UNAME_S), execute ./$(GUITARGET)"

atikserver: atikserver.o gpiodev/gpiodev.o
	$(CXX) $(ATIKCXXFLAGS) -o atikserver.out gpiodev/gpiodev.o atikserver.o $(ATIKLDFLAGS)

$(GUITARGET): $(CPPOBJS) imgui/libimgui_glfw.a
	$(CXX) $(CXXFLAGS) -o $@ $(CPPOBJS) imgui/libimgui_glfw.a $(LIBS)

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
