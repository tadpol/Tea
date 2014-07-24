
CFLAGS+= -Os
#CFLAGS+= -g

all: tea-test

tea-test: tea.o main.o
	$(CC) $(CFLAGS) -o $@ $^ -lm
	@ls -lh

test: tea-test
	./tea-test

clean:
	rm -f tea.o main.o tea-test

