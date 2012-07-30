
#CFLAGS+=-g -Os -DTEST_IT -DUSE_STDOUT
CFLAGS+= -Os -DTEST_IT

all: teash.o

teash: teash.o
	$(CC) $(CFLAGS) -o $@ $^
	@ls -lh

clean:
	rm -f *.o teash

