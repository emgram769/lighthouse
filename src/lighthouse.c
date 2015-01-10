#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

#include <unistd.h>
#include <sys/types.h>

#include <xcb/xcb.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>
#include <pthread.h>

#include <xcb_keysyms.h>

#define HEIGHT      30
#define MAX_HEIGHT  800
#define WIDTH       500
#define FONT_SIZE   18
#define MAX_QUERY   1024

#define min(a,b) ((a) < (b) ? (a) : (b))

typedef struct {
  float r;
  float g;
  float b;
} color_t;

typedef struct {
  char *action;
  char *text;
  char *graphic;
} result_t;

struct result_params {
  int fd;
  cairo_t *cr;
  cairo_surface_t *cr_surface;
  xcb_connection_t *connection;
  xcb_window_t window;
};

/* Globals. */
pthread_mutex_t draw_mutex;

static inline int check_xcb_cookie(xcb_void_cookie_t cookie, xcb_connection_t *connection, char *error) {
  xcb_generic_error_t *xcb_error = xcb_request_check(connection, cookie);
  if (xcb_error) {
    fprintf(stderr, "[error:%"PRIu8"] %s\n", xcb_error->error_code, error);
    return 1;
  }

  return 0;
}

static void draw_line(cairo_t *cr, const char *text, unsigned int line, color_t *foreground, color_t *background) {
  pthread_mutex_lock(&draw_mutex);
  cairo_set_source_rgb(cr, background->r, background->g, background->b);
  cairo_rectangle(cr, 0, line * HEIGHT, WIDTH, HEIGHT);
  cairo_stroke_preserve(cr);
  cairo_fill(cr);
  cairo_move_to(cr, 5, (line + 1) * HEIGHT - FONT_SIZE/2);
  cairo_set_source_rgb(cr, foreground->r, foreground->g, foreground->b);
  cairo_select_font_face(cr, "Source Code Pro", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, FONT_SIZE);
  cairo_show_text(cr, text);
  pthread_mutex_unlock(&draw_mutex);
}

static void draw_query_text(cairo_t *cr, const char *text) {
  color_t foreground = { 0.1, 0.1, 0.1 };
  color_t background = { 1.0, 1.0, 0.7 };
  draw_line(cr, text, 0, &foreground, &background);
}

static void draw_response_text(xcb_connection_t *connection, xcb_window_t window, cairo_t *cr, cairo_surface_t *surface, result_t *results, unsigned int result_count) {

  unsigned int new_height = min(HEIGHT * (result_count + 1), MAX_HEIGHT);
  uint32_t values[] = { 200, 300, WIDTH, new_height};

  xcb_configure_window (connection, window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
  cairo_xcb_surface_set_size(surface, WIDTH, new_height);

  color_t foreground = { 0.1, 0.1, 0.1 };
  color_t background = { 0.6, 1.0, 0.7 };

  int i;
  for (i = 0; i < result_count; i++) {
    draw_line(cr, results[i].text, i + 1, &foreground, &background);
  }

  (void)values;
  (void)cr;
  (void)results;
}

unsigned int parse_response_text(char *text, size_t length, result_t **results) {
  int index, mode;
  mode = 0; /* 0 -> closed, 1 -> opened no command, 2 -> opened, command */
  result_t *ret = calloc(1, sizeof(result_t));
  unsigned int count = 0;
  for (index = 0; text[index] != 0; index++) {
    /* Opening brace. */
    if (text[index] == '{') {
      if (mode != 0) {
        fprintf(stderr, "Syntax error.");
        free(ret);
        return 0;
      }
      count++;
      ret = realloc(ret, count * sizeof(result_t));
      if (index + 1 < length) {
        ret[count - 1].text = &text[index+1];
      }
      mode++;
    }
    /* Split brace. */
    else if (text[index] == '|') {
      if (mode != 1) {
        fprintf(stderr, "Syntax error.");
        free(ret);
        return 0;
      }
      text[index] = 0;
      if (index + 1 < length) {
        ret[count - 1].action = &text[index+1];
      }
      mode++;
    }
    /* Closing brace. */
    else if (text[index] == '}') {
      if (mode == 0) {
        fprintf(stderr, "Syntax error.\n");
        free(ret);
        return 0;
      }
      text[index] = 0;
      mode = 0;
    }
  }
  *results = ret;
  return count;
}

void *get_results(void *args) {
  int fd = ((struct result_params *)args)->fd;
  cairo_t *cairo_context = ((struct result_params *)args)->cr;
  cairo_surface_t *cairo_surface = ((struct result_params *)args)->cr_surface;
  xcb_connection_t *connection = ((struct result_params *)args)->connection;
  xcb_window_t window = ((struct result_params *)args)->window;

  char buf[100 * 1024];

  while (1) {
    size_t res = read(fd, buf, sizeof(buf));
    if (res <= 0) {
      fprintf(stderr, "Error in spawned cmd.\n");
      exit(1);
    }
    printf("%s\n", buf);
    result_t *results;
    unsigned int result_count = parse_response_text(buf, res, &results);
    draw_response_text(connection, window, cairo_context, cairo_surface, results, result_count);
    free(results);
    memset(buf, 0, sizeof(buf));
  }
}

int write_to_remote(FILE *child, char *format, ...) {
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

static inline void process_key_stroke(char *query_buffer, unsigned int *query_index, xcb_keysym_t key, cairo_t *cairo_context, FILE *to_write) {

  if (key == 65288 && *query_index > 0) {
    query_buffer[--(*query_index)] = 0;
  } else if (isprint(key) && *query_index < MAX_QUERY) {
    query_buffer[(*query_index)++] = key;
  }

  draw_query_text(cairo_context, query_buffer);
  if (write_to_remote(to_write, "%s\n", query_buffer)) {
    fprintf(stderr, "Failed to write.\n");
  }
}

int spawn_piped_process(char *file, int *to_child_fd, int *from_child_fd) {
  /* Create pipes for IPC with the user process. */
  int in_pipe[2];
  int out_pipe[2];
  pid_t childpid;

  if (pipe(in_pipe)) {
    fprintf(stderr, "Couldn't create pipe 1.\n");
    return -1;
  }

  if (pipe(out_pipe)) {
    fprintf(stderr, "Couldn't create pipe 2.\n");
    return -1;
  }

  /* Execute the user process. */
  if ((childpid = fork()) == -1) {
    fprintf(stderr, "Couldn't spawn cmd.\n");
    return -1;
  }

  if (childpid == 0) {
    close(in_pipe[1]);
    dup2(in_pipe[0], STDIN_FILENO);
    dup2(out_pipe[1], STDOUT_FILENO);

    execlp(file, file, NULL);
    fprintf(stderr, "Couldn't execute file.\n");
    close(out_pipe[1]);
    close(in_pipe[0]);
    return -1;
  }

  /* We don't need to read from in_pipe or write to out_pipe. */
  close(in_pipe[0]);
  close(out_pipe[1]);

  *from_child_fd = out_pipe[0];
  *to_child_fd = in_pipe[1];

  return 0;
}

int main(int argc, char **argv) {
  int exit_code = 0;

  /* Set up the remote process. */
  int to_child_fd, from_child_fd;

  char exec_file[] = "/home/bwasti/.config/lighthouse/cmd.py";

  if (spawn_piped_process(exec_file, &to_child_fd, &from_child_fd)) {
    fprintf(stderr, "Failed to spawn piped process.\n");
    exit_code = 1;
    return exit_code;
  }

  /* The main way to communicate with our remote process. */ 
  FILE *to_child = fdopen(to_child_fd, "w");

  /* Connect to the X server. */
  xcb_connection_t *connection = xcb_connect(NULL, NULL);

  /* Setup keyboard stuff. Thanks Apple! */
  xcb_key_symbols_t *keysyms = xcb_key_symbols_alloc(connection);

  /* Get the first screen. */
  const xcb_setup_t *setup = xcb_get_setup(connection);
  xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
  xcb_screen_t *screen = iter.data;

  /* Create a window. */
  xcb_window_t window = xcb_generate_id(connection);
  uint32_t values[2];
  values[0] = screen->white_pixel;
  values[1] = XCB_EVENT_MASK_EXPOSURE
            | XCB_EVENT_MASK_KEY_PRESS
            | XCB_EVENT_MASK_KEY_RELEASE;
  xcb_void_cookie_t window_cookie = xcb_create_window_checked(connection,
    XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, WIDTH, HEIGHT, 1,
    XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
    XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values);

  if (check_xcb_cookie(window_cookie, connection, "Failed to initialize window.")) {
    exit_code = 1;
    goto cleanup;
  }

  /* Find the visualtype by iterating through depths. */
  xcb_visualtype_t *visual = NULL;
  xcb_depth_iterator_t depth_iter = xcb_screen_allowed_depths_iterator(screen);
  for (; depth_iter.rem; xcb_depth_next(&depth_iter)) {
    xcb_visualtype_iterator_t visual_iter = xcb_depth_visuals_iterator(depth_iter.data);
    for (; visual_iter.rem; xcb_visualtype_next(&visual_iter)) {
      if (screen->root_visual == visual_iter.data->visual_id) {
        visual = visual_iter.data;
        break;
      }
    }
  }
  if (visual == NULL) {
    goto cleanup;
  }

  /* Create cairo stuff. */
  cairo_surface_t *cairo_surface = cairo_xcb_surface_create(connection, window,visual, WIDTH, HEIGHT);
  if (cairo_surface == NULL) {
    goto cleanup;
  }

  cairo_t *cairo_context = cairo_create(cairo_surface);
  if (cairo_context == NULL) {
    cairo_surface_destroy(cairo_surface);
    goto cleanup;
  }

  /* Spawn a thread to listen to our remote process. */
  if (pthread_mutex_init(&draw_mutex, NULL)) {
    fprintf(stderr, "Failed to create mutex.");
    goto cleanup;
  }

  pthread_t results_thr;
  struct result_params results_thr_params;
  results_thr_params.fd = from_child_fd; 
  results_thr_params.cr = cairo_context;
  results_thr_params.cr_surface = cairo_surface;
  results_thr_params.connection = connection;
  results_thr_params.window = window;

  if (pthread_create(&results_thr, NULL, &get_results, &results_thr_params)) {
    fprintf(stderr, "Couldn't spawn second thread.\n");
    exit(1);
  }


/*  cairo_font_options_t *cairo_font_options = cairo_font_options_create();
  cairo_font_options_set_antialias(cairo_font_options, CAIRO_ANTIALIAS_SUBPIXEL);
*/
  xcb_map_window(connection, window);
  xcb_flush(connection);

  /* Query string. */
  char query_string[MAX_QUERY];
  memset(query_string, 0, sizeof(query_string));
  unsigned int query_index = 0;

  xcb_generic_event_t *event;
  while ((event = xcb_wait_for_event(connection))) {
    switch (event->response_type & ~0x80) {
      case XCB_EXPOSE:
        draw_query_text(cairo_context, query_string);
        xcb_flush(connection);
        break;
      case XCB_KEY_PRESS: {
        break;
      }
      case XCB_KEY_RELEASE: {
        xcb_key_release_event_t *k = (xcb_key_release_event_t *)event;
        xcb_keysym_t key = xcb_key_press_lookup_keysym(keysyms, k, k->state);
        process_key_stroke(query_string, &query_index, key, cairo_context, to_child);
        xcb_flush(connection);
        break;
      }
      default:
        break;
    }

    free(event);
  }

//cleanup_cairo:
  cairo_surface_destroy(cairo_surface);
  cairo_destroy(cairo_context);

cleanup:
  xcb_disconnect(connection);
  xcb_key_symbols_free(keysyms);

  return exit_code;
}

