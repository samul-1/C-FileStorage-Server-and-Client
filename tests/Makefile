# Makefile

CC = gcc

# Compiler flags: all warnings + debugger meta-data
CFLAGS = -Wall -g -std=c99 -pthread


# The final executable program file, i.e. name of our program
BIN = cachealgos

# Object files from which $BIN depends
OBJS = ../obj/filesystemApi.o ../obj/log.o ../obj/boundedbuffer.o ../obj/cacheFns.o ../obj/icl_hash.o

# Path of Object files
OBJDIR = ../obj

SRCDIR = ../src
HEADDIR = ../include
# Libraries
LIBS = -lpthread

HEADERS =

# This default rule compiles the executable program
$(BIN): $(OBJS) $(BIN).c $(HEADERS)
	$(CC) $(CFLAGS) $(LIBS) $(OBJS) $(BIN).c -o $(BIN)

# This rule compiles each module into its object file
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(HEADDIR)/%.h
	$(CC) -c $(CFLAGS) $(LIBS) $< -o $@


clean:
	rm -f *~ $(OBJDIR)/*.o $(BIN)