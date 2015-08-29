#ifndef _CHILD_H
#define _CHILD_H

#include <stdint.h>
#include <stdio.h>

/* @brief Reads from the child process's standard out in a loop.  Meant to be used
 *        as a spawned thread.
 *
 * @param args Immediately cast to a result_params_t type struct.  See that struct
 *        for more information.
 * @return NULL.
 */
void *get_results(void *args);
int32_t write_to_remote(FILE *child, char *format, ...);
int32_t spawn_piped_process(char *file, int32_t *to_child_fd, int32_t *from_child_fd, char **argv);

#endif /* _CHILD_H */
