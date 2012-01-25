
CFLAGS+=-g -Os -DTEST_IT -DUSE_STDOUT

all: tea-test

tea-test: tea18.o
	$(CC) $(CFLAGS) -o $@ $^
	@ls -lh

clean:
	rm -f tea18.o tea-test

