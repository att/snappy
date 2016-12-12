TARGET = xcore
LIBS = -Wl,-Bstatic -lmysqlclient -lssl -lcrypto -lz -Wl,-Bdynamic -lpthread -ldl -lm
CC = gcc
CFLAGS = -g -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function -I/usr/include/mysql
#LDFLAGS = -L/opt/mariadb/usr/lib64
.PHONY: default all clean

all: $(TARGET)

OBJECTS := $(patsubst %.c, %.o, $(wildcard *.c))
OBJECTS := $(filter-out import.o get.o patch.o diff.o rstr_single_full.o a.o jsmn.o, $(OBJECTS))
HEADERS = $(wildcard *.h)

%.o: %.c $(HEADERS)
	    $(CC) $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	    $(CC) $(LDFLAGS) $(OBJECTS)  -Wall $(LIBS) -o $@

clean:
	    rm -f *.o
	    rm -f $(TARGET)
