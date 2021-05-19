# Makefile

CC = gcc

# Compiler flags: all warnings + debugger meta-data
CFLAGS = -Wall -g -std=c99 -pthread -Wno-missing-braces


# The final executable program file, i.e. name of our program
BIN = server dummyclient2 dummyclient client
BINDIR = build

# Object files from which $BIN depends
OBJS = obj/filesystemApi.o obj/log.o obj/boundedbuffer.o obj/cacheFns.o obj/icl_hash.o obj/fileparser.o obj/clientApi.o obj/cliParser.o obj/clientInternals.o obj/rleCompression.o

# Path of Object files
OBJDIR = obj

SRCDIR = src
HEADDIR = include
# Libraries
LIBS = -lpthread -lm

.PHONY: all clean

all:	$(BIN)

# This default rule compiles the executable program
$(BIN): $(SRCDIR)/$< $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(SRCDIR)/$@.c $(LIBS) -o $(BINDIR)/$@

# This rule compiles each module into its object file
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(HEADDIR)/%.h
	$(CC) -c $(CFLAGS) $< -o $@ $(LIBS)


clean:
	rm -f *~ $(OBJDIR)/*.o $(BINDIR)/*