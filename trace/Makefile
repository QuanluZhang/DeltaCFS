CPPFLAGS = -Wall -lrt -std=c++11 -lpthread
PROGRAMS = replay
GEARMANFLAGS = $(shell pkg-config --cflags --libs gearmand)

all: $(PROGRAMS)

replay: replay.cpp
	g++ -o $@ $< $(CPPFLAGS)

clean:
	rm -f *.o $(PROGRAMS)