
CFLAGS+=-g -DTEST_IT
#CFLAGS+= -Os -DTEST_IT

all: teash

teash: teash.o
	$(CC) $(CFLAGS) -o $@ $^
	@ls -lh

clean:
	rm -f *.o teash

