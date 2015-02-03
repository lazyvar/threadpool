##
# Temporary Makefile
#
# Treat warnings as errors. This seems to be the only way to 
# convince some students of the importance of ensuring that
# their code compiles without warnings before starting to debug.
#
# Do not change this line.  We will not use your copy of the Makefile 
# we will use *this* Makefile to run check.py when grading.
#
# for benchmarking
#CFLAGS=-Wall -O3 -Werror -Wmissing-prototypes -fopenmp
# for debugging
CFLAGS=-Wall -O0 -g -Werror -Wmissing-prototypes

CXXFLAGS=-Wall -O3 -Werror -fopenmp
#CXXFLAGS=-Wall -g -Werror -fopenmp

LDLIBS=-lpthread -lrt

OBJ=threadpool.o list.o threadpool_lib.o

ALL=quicksort psum_test fib_test mergesort threadpool_test nqueens threadpool_test2 threadpool_test3
all: $(ALL)

threadpool_test3: threadpool_test3.o $(OBJ)

threadpool_test2: threadpool_test2.o $(OBJ)

threadpool_test: threadpool_test.o $(OBJ)

quicksort: quicksort.o $(OBJ)

nqueens: nqueens.o $(OBJ)

mergesort: mergesort.o $(OBJ)

mergesort-gnu: mergesort-gnu.o $(OBJ)
	g++ $(CXXFLAGS) -o mergesort-gnu mergesort-gnu.o $(OBJ) $(LDLIBS)

psum_test: psum_test.o $(OBJ)

fib_test: fib_test.o $(OBJ)

clean:
	rm -f *.o $(ALL)

