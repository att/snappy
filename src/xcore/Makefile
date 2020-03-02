GIT_HASH := "$(shell git rev-parse HEAD)"
COMPILE_TIME := "$(shell date -u +'%Y-%m-%d_%H:%M:%S_UTC')"
GIT_BRANCH := "$(shell git branch | grep "^\*" | sed 's/^..//')"
VERSION_FLAGS=-DGIT_HASH="\"$(GIT_HASH)\"" -DCOMPILE_TIME="\"$(COMPILE_TIME)\"" -DGIT_BRANCH="\"$(GIT_BRANCH)\""

TARGET = xcore
SNPY_LIB = ../libs/libsnpy.a
LIBS = -lsnpy -lmysqlclient -lssl -lcrypto -lz -lm -lpthread -ldl
CC = gcc
CFLAGS = $(VERSION_FLAGS) -Os -g -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function -I/usr/include/mysql -I../libs -D_GNU_SOURCE
LDFLAGS = -L../libs/ -static
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
