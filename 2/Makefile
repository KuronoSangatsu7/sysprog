GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant
LEAK_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant -ldl -rdynamic
RELAXED_FLAGS = -Wextra -Wall -Wno-gnu-folding-constant

all: parser.c solution.c
	gcc $(GCC_FLAGS) parser.c solution.c

leaks: parser.c solution.c ../utils/heap_help/heap_help.c
	gcc $(LEAK_FLAGS) parser.c solution.c ../utils/heap_help/heap_help.c

debug: parser.c solution.c
	gcc $(RELAXED_FLAGS) parser.c solution.c -g

relaxed: parser.c solution.c
	gcc $(RELAXED_FLAGS) parser.c solution.c

test:
	python3 checker.py

clean:
	rm -f a.out
