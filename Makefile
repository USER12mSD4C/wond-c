CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
TARGET = utmc
OBJS = utmc.o lexer.o parser.o ast.o codegen.o

all: $(TARGET)

utmc: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) *.asm *.o a.out /tmp/utmc_*

install: all
	install -Dm755 utmc /usr/local/bin/utmc

uninstall:
	rm -f /usr/local/bin/utmc

.PHONY: all clean install uninstall
