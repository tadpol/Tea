
#CFLAGS+=-g -DTEST_IT -DUSE_STDOUT
CFLAGS+= -Os -DTEST_IT -DUSE_STDOUT

all: tea-test

tea-test: tea.o main.o
	$(CC) $(CFLAGS) -o $@ $^
	@ls -lh

test: tea-test
	./tea-test

clean:
	rm -f tea.o main.o tea-test

