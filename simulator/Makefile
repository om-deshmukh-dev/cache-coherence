################################################################################
#              Makefile for the Cache Simulator Cache Simulator                        #
################################################################################

# Additional flags for the compiler
# always enable debugging because its more convenient
CFLAGS := -std=c99 -D_GNU_SOURCE -Wall -g3
LFLAGS := -lm

.PHONY: all clean run

all: clean cache-sim

cache-sim: cache.o cache_stats.o simulator.o print_helpers.o
	gcc $(CFLAGS) -o $@ main.c $^ $(LFLAGS)

# Wildcard rule that allows for the compilation of a *.c file to a *.o file
%.o : %.c
	gcc -c $(CFLAGS) $< -o $@

# Removes any executables and compiled object files
clean:
	rm -f cache-sim *.o
