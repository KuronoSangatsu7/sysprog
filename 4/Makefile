GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant
LEAK_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant -ldl -rdynamic

all: test.o thread_pool.o
	gcc $(GCC_FLAGS) test.o thread_pool.o

leaks: test.o thread_pool.o ../utils/heap_help/heap_help.c
	gcc $(LEAK_FLAGS) test.o thread_pool.o ../utils/heap_help/heap_help.c

test.o: test.c
	gcc $(GCC_FLAGS) -c test.c -o test.o -I ../utils

thread_pool.o: thread_pool.c
	gcc $(GCC_FLAGS) -c thread_pool.c -o thread_pool.o

clean:
	rm -f *.o a.out
