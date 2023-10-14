#include "parser.h"
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void
execute_command(const struct expr *e, bool read_from_pipe, bool write_to_pipe, int* pipedes)
{
	char* argv_arr[e->cmd.arg_count + 2];
	argv_arr[0] = e->cmd.exe;

	for (uint32_t i = 0; i < e->cmd.arg_count; ++i)
		argv_arr[i + 1] = e->cmd.args[i];

	argv_arr[e->cmd.arg_count + 1] = NULL;

	if (strcmp(e->cmd.exe, "exit") == 0)
	// TODO: handle exit | *some command*
		exit(0);
	
	else if (strcmp(e->cmd.exe, "cd") == 0)
		chdir(e->cmd.args[0]);

	else {
		pid_t p = fork();

		if (p == 0) {
			if (read_from_pipe)
				dup2(pipedes[0], 0);

			if (write_to_pipe)
				dup2(pipedes[1], 1);

			execvp(e->cmd.exe, argv_arr);

			close(pipedes[0]);
			close(pipedes[1]);
		}
	}
	
	return;
}

static void
execute_command_line(const struct command_line *line)
{
	/* REPLACE THIS CODE WITH ACTUAL COMMAND EXECUTION */

	assert(line != NULL);
	printf("================================\n");
	printf("Command line:\n");
	printf("Is background: %d\n", (int)line->is_background);
	printf("Output: ");
	if (line->out_type == OUTPUT_TYPE_STDOUT) {
		printf("stdout\n");
	} else if (line->out_type == OUTPUT_TYPE_FILE_NEW) {
		printf("new file - \"%s\"\n", line->out_file);
	} else if (line->out_type == OUTPUT_TYPE_FILE_APPEND) {
		printf("append file - \"%s\"\n", line->out_file);
	} else {
		assert(false);
	}
	printf("Expressions:\n");
	const struct expr *e = line->head;

	int pipedes[2];
	bool read_from_pipe = false, write_to_pipe = false;

	while (e != NULL) {
		if (e->type == EXPR_TYPE_COMMAND) {
			printf("\tCommand: %s", e->cmd.exe);
			for (uint32_t i = 0; i < e->cmd.arg_count; ++i)
				printf(" %s", e->cmd.args[i]);
			printf("\n");

			if (e->next != NULL && e->next->type == EXPR_TYPE_PIPE) {
				pipe(pipedes);
				write_to_pipe = true;
			}

			// if (e->next == NULL) {
			// 	if (line->out_type == OUTPUT_TYPE_FILE_NEW) {

			// 	}

			// 	if (line->out_type == OUTPUT_TYPE_FILE_APPEND) {

			// 	}
			// }

			execute_command(e, read_from_pipe, write_to_pipe, pipedes);
			
			write_to_pipe = false;
			read_from_pipe = false;
			
		} else if (e->type == EXPR_TYPE_PIPE) {
			read_from_pipe = true;

			printf("\tPIPE\n");
		} else if (e->type == EXPR_TYPE_AND) {
			printf("\tAND\n");
		} else if (e->type == EXPR_TYPE_OR) {
			printf("\tOR\n");
		} else {
			assert(false);
		}
		e = e->next;
	}
}

int
main(void)
{
	const size_t buf_size = 1024;
	char buf[buf_size];
	int rc;
	struct parser *p = parser_new();
	while ((rc = read(STDIN_FILENO, buf, buf_size)) > 0) {
		parser_feed(p, buf, rc);
		struct command_line *line = NULL;
		while (true) {
			enum parser_error err = parser_pop_next(p, &line);
			if (err == PARSER_ERR_NONE && line == NULL)
				break;
			if (err != PARSER_ERR_NONE) {
				printf("Error: %d\n", (int)err);
				continue;
			}
			execute_command_line(line);
			command_line_delete(line);
		}
	}
	parser_delete(p);
	return 0;
}
