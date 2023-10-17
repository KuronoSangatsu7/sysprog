#include "parser.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * A function that returns the number of pipes in a command line
 */
static int get_num_pipes(const struct command_line *line) {
  int num_pipes = 0;
  struct expr *expr = line->head;
  while (expr != NULL) {
    if (expr->type == EXPR_TYPE_PIPE)
      num_pipes++;
    expr = expr->next;
  }
  return num_pipes;
}

/*
 * A function that returns the number of commands in a command line
 */
static int get_num_commands(const struct command_line *line) {
  int num_commands = 0;
  struct expr *expr = line->head;
  while (expr != NULL) {
    if (expr->type == EXPR_TYPE_COMMAND)
      num_commands++;
    expr = expr->next;
  }
  return num_commands;
}

/*
 * A function that, given a command line and number of pipes
 * initializes an array of pipes as well as a file descriptor
 * for redirects if any given as parameters
 */
static void initialize_pipes(const struct command_line *line, int num_pipes,
                             int *redirect_fd, int (*pd)[2]) {
  for (int i = 0; i < num_pipes; i++)
    pipe(pd[i]);

  if (line->out_type == OUTPUT_TYPE_FILE_NEW)
    *redirect_fd = open(line->out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

  else if (line->out_type == OUTPUT_TYPE_FILE_APPEND)
    *redirect_fd = open(line->out_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
}

/*
 * A function that closes a list of pipes as well
 * as a file descriptor for redirects if any given as parameters
 */
static void delete_pipes(int num_pipes, int *redirect_fd, int (*pd)[2]) {
  for (int i = 0; i < num_pipes; i++) {
    close(pd[i][0]);
    close(pd[i][1]);
  }

  if (*redirect_fd != -1)
    close(*redirect_fd);
}

/*
 * A function that executes commands in a command line
 */
static int execute_commands(const struct command_line *line, int num_pipes,
                            int *redirect_fd, int (*pd)[2]) {
  struct expr *e = line->head;
  int index = 0, last_exit_code = 0;
  while (e != NULL) {
    if (e->type == EXPR_TYPE_COMMAND) {
      // Initialize the command in an array format for execvp
      char *argv_arr[e->cmd.arg_count + 2];
      argv_arr[0] = e->cmd.exe;

      for (uint32_t i = 0; i < e->cmd.arg_count; ++i)
        argv_arr[i + 1] = e->cmd.args[i];

      argv_arr[e->cmd.arg_count + 1] = NULL;

      // Manually handle "cd"
      if (strcmp(e->cmd.exe, "cd") == 0) {
        chdir(e->cmd.args[0]);
      } else {

        // fork and configure pipes in child process
        pid_t p = fork();
        if (p == 0) {
          if (index > 0) {
            dup2(pd[index - 1][0], STDIN_FILENO);
          }

          if (index < num_pipes) {
            dup2(pd[index][1], STDOUT_FILENO);
          }

          // Handle redirects if any
          if (index == num_pipes && *redirect_fd != -1) {
            dup2(*redirect_fd, STDOUT_FILENO);
          }

          // Close pipes in child process
          for (int i = 0; i < num_pipes; i++) {
            close(pd[i][0]);
            close(pd[i][1]);
          }

          // Use a dummy executable in place of exit (it will clean up its memory)
          if (strcmp(e->cmd.exe, "exit") == 0) {
            char *_true[2] = {"true", NULL};
            execvp("true", _true);
          }

          execvp(e->cmd.exe, argv_arr);
        }
      }

      index++;
    }

    e = e->next;
  }

  // Close pipes in parent process 
  delete_pipes(num_pipes, redirect_fd, pd);

  // Wait for all children to finish and save the last exit code
  for (int i = 0; i < num_pipes + 1; i++) {
    int status = 0;
    wait(&status);
    last_exit_code = status;

    if (status > 0)
      last_exit_code = WEXITSTATUS(status);
  }

  return last_exit_code;
}

/*
 * A function that executes a command line
 */
static int execute_command_line(const struct command_line *line) {

  int num_pipes = get_num_pipes(line);

  int redirect_fd = -1;
  int pd[num_pipes][2];
  int last_exit_code = 0;

  // Initialize pipes then execute, save the last exit code
  initialize_pipes(line, num_pipes, &redirect_fd, pd);
  last_exit_code = execute_commands(line, num_pipes, &redirect_fd, pd);

  return last_exit_code;
}

int main(void) {
  const size_t buf_size = 1024;
  char buf[buf_size];
  int rc;
  int exit_code = 0;
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

      int num_commands = get_num_commands(line);

      // Handle case where exit is the only command in the line
      if (num_commands == 1 && strcmp(line->head->cmd.exe, "exit") == 0) {
        int exit_code = 0;
        if (line->head->cmd.arg_count)
          exit_code = atoi(line->head->cmd.args[0]);

        command_line_delete(line);
        parser_delete(p);

        exit(exit_code);
      }

      exit_code = execute_command_line(line);

      // Handle case where exit is the last command in the line 
      // get its exit code to exit with it later
      if (strcmp(line->tail->cmd.exe, "exit") == 0) {
        if (line->tail->cmd.arg_count)
          exit_code = atoi(line->tail->cmd.args[0]);

        else
          exit_code = 0;
      }

      command_line_delete(line);
    }
  }
  parser_delete(p);
  return exit_code;
}
