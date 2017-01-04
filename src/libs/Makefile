TARGET = libsnpy.a
CC = gcc
CFLAGS = -g -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function 
.PHONY: default all clean

all: $(TARGET)

OBJECTS := $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS = $(wildcard *.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	ar ru $@ $^
	ranlib $@

install: 
	install -s -m 0755 $(TARGET) /var/lib/snappy/bin/
	

clean:
	rm -f *.o
	rm -f $(TARGET)
