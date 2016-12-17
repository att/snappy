TARGET = snpy_rbd

#gcc -O2  -static -I/opt/ceph/usr/include -I/opt/boost/usr/include -L./libs snpy_rbd.c json.c util.c -lmysqlclient -lrados -lrbd -lboost_system -lboost_thread -lcrypto++  -lstdc++   -lpthread -lm -ldl
LIBS = -Wl,-Bstatic -lmysqlclient -lrados -lrbd -lboost_system -lboost_thread -lcrypto++  -lstdc++  -Wl,-Bdynamic  -lpthread -lm -ldl
#LIBS = -lrados -lrbd -lboost_system -lboost_thread -lcryptopp -lstdc++ -lpthread -lm -ldl
CC = gcc
CFLAGS = -g -Wall -Wno-unused-variable -Wno-unused-function  -I/opt/ceph/usr/include
LDFLAGS = -L./libs

.PHONY: default all clean

all: $(TARGET)

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
OBJECTS := $(filter-out a.o, $(OBJECTS))
HEADERS = $(wildcard *.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS)  -Wall $(LIBS) -o $@

clean:
	rm -f *.o
	rm -f $(TARGET)

