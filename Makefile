CC=gcc
CFLAGS=--std=c99 -pedantic -Wall `pkg-config --cflags gtk4`
SOURCES=$(wildcard *.c)
OBJECTS=$(SOURCES:.c=.o)
DEST=.
EXE=play
INCLUDES=
LIBS=-lm -lasound `pkg-config --libs gtk4`

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

exe: $(OBJECTS)
	$(CC) $(CFLAGS) $(LIBS) $(OBJECTS) -o $(DEST)/$(EXE)

debug: CFLAGS += -g
debug: exe

profile: CFLAGS += -g -pg -O3
profile: exe

coverage: CFLAGS += -fprofile-arcs -ftest-coverage
coverage: exe

release: CFLAGS += -O3
release: exe

clean:
	@ - rm $(DEST)/$(EXE) $(OBJECTS)
