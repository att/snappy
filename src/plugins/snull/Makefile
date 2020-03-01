TARGET = snpy_snull

LIBS = -lsnpy -lm
CC = gcc
CFLAGS = -Os -Wall -Wno-unused-variable -Wno-unused-function  -I./include -I../../libs/
LDFLAGS = -L./libs -L../../libs/ -static

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

