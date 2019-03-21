TARGET = libsnpy.a
CC = gcc
CFLAGS = -Os -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function 
.PHONY: default all clean

all: $(TARGET) test

OBJECTS := $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS = $(wildcard *.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	ar ru $@ $^
	ranlib $@

test:	$(TARGET)
	$(CC) $(CFLAGS) -o libsnpy_test libsnpy_test.c $(TARGET) -lm

clean:
	rm -f *.o
	rm -f $(TARGET)
