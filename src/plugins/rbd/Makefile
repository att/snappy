TARGET = snpy_rbd

SNPY_LIB = ../../libs/libsnpy.a
LIBS = -static-libgcc -Wl,-Bstatic -lsnpy -lrados -lrbd -lboost_system -lboost_thread -lboost_iostreams -lboost_random -lcrypto++  -lstdc++ -lz -Wl,-Bdynamic  -lpthread -lm -ldl
#LIBS = -lrados -lrbd -lboost_system -lboost_thread -lcryptopp -lstdc++ -lpthread -lm -ldl
CC = gcc
CFLAGS = -O2 -Wall -Wno-unused-variable -Wno-unused-function  -I./include -I../../libs/
LDFLAGS = -L./libs -L../../libs/

.PHONY: default all clean

all: $(TARGET)

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
OBJECTS := $(filter-out a.o, $(OBJECTS))
HEADERS = $(wildcard *.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS) $(SNPY_LIB)
	$(CC) $(LDFLAGS) $(OBJECTS)  -Wall $(LIBS) -o $@

install:
	install  -m 0755 $(TARGET) /var/lib/snappy/plugins/rbd/

clean:
	rm -f *.o
	rm -f $(TARGET)

