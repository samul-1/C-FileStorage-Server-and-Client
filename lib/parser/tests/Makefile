# Makefile

CC = gcc

# Compiler flags: all warnings + debugger meta-data
CFLAGS = -Wall -g -std=c99


# The final executable program file, i.e. name of our program
BIN = main

# Object files from which $BIN depends
OBJS = ../fileparser.o

# Path of Object files
OBJDIR = ../

# Libraries
LIBS =

HEADERS = ../fileparser.h

# This default rule compiles the executable program
$(BIN): $(OBJS) $(BIN).c $(HEADERS)
	$(CC) $(CFLAGS) $(LIBS) $(OBJS) $(BIN).c -o $(BIN)

# This rule compiles each module into its object file
$(OBJDIR)/%.o: $(OBJDIR)/%/%.c $(OBJDIR)/%/%.h
	$(CC) -c $(CFLAGS) $(LIBS) $< -o $@


clean:
	rm -f *~ $(OBJDIR)/*.o $(BIN)
