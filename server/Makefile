CPPFLAGS = -Wall -g -std=c++11 -lpthread
GEARMANFLAGS = $(shell pkg-config --cflags --libs gearmand)
HIREDISFLAGS = $(shell pkg-config --cflags --libs hiredis)
LEVELDBFLAGS = -lleveldb
LIBRSYNCFLAGS = -lrsync
PROGRAMS = deltacfs_server_gearman

all: $(PROGRAMS)

deltacfs_server_gearman:deltacfs_server_gearman.cpp
	g++ -o deltacfs_server_gearman deltacfs_server_gearman.cpp $(CPPFLAGS) $(LIBRSYNCFLAGS) $(GEARMANFLAGS) $(HIREDISFLAGS) $(LEVELDBFLAGS)

clean:
	rm -f *.o $(PROGRAMS)
