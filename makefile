TARGET  := netservice
LIBS    :=
INCLUDE := -I . -I .. -I ~/workspace/common/inc
SOURCE3 := $(wildcard ../*.cpp)
SOURCE2 := $(wildcard ~/workspace/common/src/*.cpp)
SOURCE  := $(wildcard *.c) $(wildcard *.cpp)
OBJS    := $(patsubst %.c,%.o,$(patsubst %.cpp,%.o,$(SOURCE))) $(patsubst %.c,%.o,$(patsubst %.cpp,%.o,$(SOURCE2))) $(patsubst %.c,%.o,$(patsubst %.cpp,%.o,$(SOURCE3)))
CC      := g++
LDFLAGS :=
DEFINES :=
STD     :=
CFLAGS  := -g -rdynamic -Wall -Werror -O0 -pthread $(STD) $(DEFINES) $(INCLUDE)
CXXFLAGS:= $(CFLAGS)
#-DHAVE_CONFIG_H
.PHONY : everything objs clean veryclean rebuild
everything : clean $(TARGET)
all : $(TARGET)
objs : $(OBJS)
rebuild: veryclean everything            
clean :
	rm -fr *.so
	rm -fr *.o
veryclean : clean
	rm -fr $(TARGET)
$(TARGET) : $(OBJS)
	$(CC) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)
