TARGET = xcore
SNPY_LIB = ../libs/libsnpy.a
LIBS = -Wl,-Bstatic -lsnpy -lmysqlclient -lssl -lcrypto -lz -Wl,-Bdynamic -lpthread -ldl -lm
CC = gcc
CFLAGS = -g -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function -I/usr/include/mysql -I../libs -D_GNU_SOURCE
LDFLAGS = -L../libs/ 
.PHONY: default all clean

all: $(TARGET)

OBJECTS := $(patsubst %.c, %.o, $(wildcard *.c))
OBJECTS := $(filter-out patch.o diff.o a.o, $(OBJECTS))
HEADERS = $(wildcard *.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS) $(SNPY_LIB)
	$(CC) $(LDFLAGS) $(OBJECTS)  -Wall $(LIBS) -o $@

install: 
	install -m 0755 $(TARGET) /var/lib/snappy/bin/
	

clean:
	rm -f *.o
	rm -f $(TARGET)
