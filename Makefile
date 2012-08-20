
CFLAGS+=-g -funsigned-char -DTEST_IT
#CFLAGS+= -Os -funsigned-char -DTEST_IT

all: teash

teash: teash.o
	$(CC) $(CFLAGS) -o $@ $^
	@ls -lh

clean:
	rm -f *.o teash

