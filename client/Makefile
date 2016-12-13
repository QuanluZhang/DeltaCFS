CPPFLAGS = -Wall -g -O3
FUSEFLAGS = $(shell pkg-config --cflags --libs fuse)
GEARMANFLAGS = $(shell pkg-config --cflags --libs gearmand)
LIBRSYNCPROFLAGS = -I../librsync-mod ../librsync-mod/.libs/librsync.a
LEVELDBFLAGS = -lleveldb
OBJS = syncdelta.o syncqueue.o syncrelation.o syncsend.o syncutil.o syncchecksum.o synctimestamp.o deltacfs_client_gearman.o
TARGET = deltacfs_client_gearman

all: $(TARGET)

%.o:%.c
	gcc -o $@ -c $< $(LIBRSYNCPROFLAGS) $(CPPFLAGS) $(FUSEFLAGS) $(LEVELDBFLAGS) $(GEARMANFLAGS)

deltacfs_client_gearman:$(OBJS)
	gcc -o $@ $^ $(LIBRSYNCPROFLAGS) $(FUSEFLAGS) $(CPPFLAGS) $(LEVELDBFLAGS) $(GEARMANFLAGS)

clean:
	rm -f *.o $(TARGET)
