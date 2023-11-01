GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant
LEAK_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant -ldl -rdynamic

all: test.o userfs.o
	gcc $(GCC_FLAGS) test.o userfs.o

leaks: test.o userfs.o ../utils/heap_help/heap_help.c
	gcc $(LEAK_FLAGS) test.o userfs.o ../utils/heap_help/heap_help.c

test.o: test.c
	gcc $(GCC_FLAGS) -c test.c -o test.o -I ../utils

userfs.o: userfs.c
	gcc $(GCC_FLAGS) -c userfs.c -o userfs.o

clean:
	rm -f *.o a.out