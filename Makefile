
#CFLAGS+=-Wall -g -DTEST_IT
CFLAGS+=-Wall -Os -DTEST_IT

all: teash

teash: teash.o
	$(CC) $(CFLAGS) -o $@ $^
	@ls -lh

clean:
	rm -f *.o teash

