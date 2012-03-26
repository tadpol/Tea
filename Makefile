
#CFLAGS+=-g -DTEST_IT -DUSE_STDOUT
CFLAGS+= -Os -DTEST_IT -DUSE_STDOUT

all: tea-test

tea-test: tea.o
	$(CC) $(CFLAGS) -o $@ $^
	@ls -lh

clean:
	rm -f tea.o tea-test

