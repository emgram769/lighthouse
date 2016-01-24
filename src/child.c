/** @file child.c
 *  @author Bram Wasti <bwasti@cmu.edu>
 *  
 *  @brief This file contains the logic that is run in a separate thread
 *         to pull results from the spawned user defined process.
 */

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wordexp.h>

#include "child.h"
#include "display.h"
#include "globals.h"
#include "results.h"

/* @brief Checks if the buffer has a newline.
 *
 * If there is a newline it is replaced with a null terminator
 * and 1 is returned, otherwise 0 is returned.
 *
 * @param buf The buffer to be scanned for a newline.
 * @param len The length of the buffer.
 * @return 1 if there exists a newline in the buffer, else 0.
 */
int32_t find_newline(char *buf, size_t len) {
  uint32_t i;
  for (i = 0; i < len; i++) {
    if (buf[i] == '\n') {
      buf[i] = '\0';
      return 1;
    }
    if (buf[i] == '\0') {
      return 0;
    }
  }
  return 0;
}


void *get_results(void *args) {
  int32_t fd = ((struct result_params *)args)->fd;
  cairo_t *cairo_context = ((struct result_params *)args)->cr;
  cairo_surface_t *cairo_surface = ((struct result_params *)args)->cr_surface;
  xcb_connection_t *connection = ((struct result_params *)args)->connection;
  xcb_window_t window = ((struct result_params *)args)->window;

  int64_t res;

  while (1) {
    /* Read until a new line. */
    res = 0;
    int32_t ret;
    do {
      ret = read(fd, global.result_buf + res, sizeof(global.result_buf) - res);
      res += ret;
    } while(!find_newline(global.result_buf, sizeof(global.result_buf))
        && ret > 0);

    if (res < 0) {
      fprintf(stderr, "Error in spawned cmd.\n");
      return NULL;
    } else if (res == 0) {
      return NULL;
    }
    result_t *results = NULL;
    uint32_t result_count = parse_result_text(global.result_buf, res, &results);
    pthread_mutex_lock(&global.result_mutex);
    if (global.results && results != global.results) {
      free(global.results);
    }
    global.results = results;
    global.result_count = result_count;
    debug("Recieved %d results.\n", result_count);
    if (global.result_count) {
      draw_result_text(connection, window, cairo_context, cairo_surface, results);
    } else {
      /* If no result found, just draw an empty window. */
      uint32_t values[] = { settings.width, settings.height };
      xcb_configure_window (connection, window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
      cairo_xcb_surface_set_size(cairo_surface, settings.width, settings.height);
    }
    pthread_mutex_unlock(&global.result_mutex);
  }
}

/* @brief Writes to the passed in file descriptor.
 *
 * Note: this function is used exclusively to write to the child process.
 *
 * @param child The file descriptor to write to.
 * @return 0 on success and 1 on failure.
 */
int32_t write_to_remote(FILE *child, char *format, ...) {
  va_list args;
  va_start(args, format);
  if (vfprintf(child, format, args) < 0) {
    return -1;
  }
  va_end(args);
  if (fflush(child)) {
    return -1;
  }

  return 0;
}

/* @brief Spawns a process (via fork) and sets up pipes to allow communication with
 *        the user defined executable.
 *
 * @param file The user defined file to load into the newly spawned process.
 * @param to_child_fd The fd used to write to the child process.
 * @param from_child_fd The fd used to read from the child process.
 * @return 0 on success and 1 on failure.
 */
int32_t spawn_piped_process(char *file, int32_t *to_child_fd, int32_t *from_child_fd, char **argv) {
  /* Create pipes for IPC with the user process. */
  int32_t in_pipe[2];
  int32_t out_pipe[2];
  pid_t child_pid;

  if (pipe(in_pipe)) {
    fprintf(stderr, "Couldn't create pipe 1: %s\n", strerror(errno));
    return -1;
  }

  if (pipe(out_pipe)) {
    fprintf(stderr, "Couldn't create pipe 2: %s\n", strerror(errno));
    return -1;
  }

  /* Execute the user process. */
  if ((child_pid = fork()) == -1) {
    fprintf(stderr, "Couldn't spawn command: %s\n", strerror(errno));
    return -1;
  }

  if (child_pid == 0) {
    close(in_pipe[1]);
    dup2(in_pipe[0], STDIN_FILENO);
    dup2(out_pipe[1], STDOUT_FILENO);

    wordexp_t expanded_file;
    if (wordexp(file, &expanded_file, 0)) {
      fprintf(stderr, "Error expanding file %s\n", file);
    } else {
      file = expanded_file.we_wordv[0];
    }

    argv[0] = file;
    execvp(file, (char * const *)argv);
    fprintf(stderr, "Couldn't execute file '%s': %s\n", file, strerror(errno));
    close(out_pipe[1]);
    close(in_pipe[0]);
    return -1;
  }

  global.child_pid = child_pid;

  /* We don't need to read from in_pipe or write to out_pipe. */
  close(in_pipe[0]);
  close(out_pipe[1]);

  *from_child_fd = out_pipe[0];
  *to_child_fd = in_pipe[1];
  return 0;
}

