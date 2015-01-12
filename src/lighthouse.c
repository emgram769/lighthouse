#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <inttypes.h>
#include <wordexp.h>

#include <unistd.h>
#include <sys/types.h>

#include <xcb/xcb.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>
#include <pthread.h>

#include <xcb_keysyms.h>

#define HEIGHT            30
#define MAX_HEIGHT        800
#define WIDTH             500
#define FONT_SIZE         18
#define MAX_QUERY         1024
#define MAX_CONFIG_SIZE   10*1024
#define CONFIG_FILE       "./.config/lighthouse/lighthouserc"

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

typedef struct {
  /* The color scheme. */
  color_t query_fg;
  color_t query_bg;
  color_t result_fg;
  color_t result_bg;
  color_t highlight_fg;
  color_t highlight_bg;

  /* The process to pipe input to. */
  char *cmd;

  /* Font. */
  char *font_name;
  unsigned int font_size;

  /* Size. */
  unsigned int height;
  unsigned int max_height;
  unsigned int width;
  unsigned int screen_height;
  unsigned int screen_width;
  /* Percentage offset ont the screen. */
  unsigned int x;
  unsigned int y;
} settings_t;

static char config_buf[MAX_CONFIG_SIZE];
static settings_t settings;

/* Globals. */
struct {
  pthread_mutex_t draw_mutex;
  pthread_mutex_t result_mutex;
  char result_buf[1024 * 100];
  result_t *results;
  unsigned int result_count;
  unsigned int result_highlight;
} global;

static inline int check_xcb_cookie(xcb_void_cookie_t cookie, xcb_connection_t *connection, char *error) {
  xcb_generic_error_t *xcb_error = xcb_request_check(connection, cookie);
  if (xcb_error) {
    fprintf(stderr, "[error:%"PRIu8"] %s\n", xcb_error->error_code, error);
    return 1;
  }

  return 0;
}

static void draw_typed_line(cairo_t *cr, char *text, unsigned int line, unsigned int cursor, color_t *foreground, color_t *background) {
  pthread_mutex_lock(&global.draw_mutex);

  /* Set the background. */
  cairo_set_source_rgb(cr, background->r, background->g, background->b);
  cairo_rectangle(cr, 0, line * settings.height, settings.width, settings.height);
  cairo_stroke_preserve(cr);
  cairo_fill(cr);

  /* Set the foreground color and font. */
  cairo_set_source_rgb(cr, foreground->r, foreground->g, foreground->b);
  cairo_select_font_face(cr, settings.font_name, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

  unsigned int x_offset = 5;
  /* Find the cursor relative to the text. */
  cairo_text_extents_t extents;
  char saved_char = text[cursor];
  text[cursor] = '\0';
  cairo_text_extents(cr, text, &extents);
  text[cursor] = saved_char;
  unsigned int cursor_x = extents.x_advance;

  /* Find the text offset. */
  cairo_text_extents(cr, text, &extents);
  if (settings.width < extents.width) {
    x_offset = settings.width - extents.x_advance - x_offset;
  }
  cursor_x += x_offset;

  /* Draw the text. */
  cairo_move_to(cr, x_offset, (line + 1) * settings.height - settings.font_size/2);
  cairo_set_font_size(cr, settings.font_size);
  cairo_show_text(cr, text);

  /* Draw the cursor. */
  cairo_set_source_rgb(cr, foreground->r, foreground->g, foreground->b);
  cairo_rectangle(cr, cursor_x + 4, line * settings.height, 0, settings.height);
  cairo_stroke_preserve(cr);
  cairo_fill(cr);

  pthread_mutex_unlock(&global.draw_mutex);
}

static void draw_line(cairo_t *cr, const char *text, unsigned int line, color_t *foreground, color_t *background) {
  pthread_mutex_lock(&global.draw_mutex);

  cairo_set_source_rgb(cr, background->r, background->g, background->b);
  cairo_rectangle(cr, 0, line * settings.height, settings.width, settings.height);
  cairo_stroke_preserve(cr);
  cairo_fill(cr);
  cairo_text_extents_t extents;
  cairo_text_extents(cr, text, &extents);
  unsigned int x_offset = 5;
  cairo_move_to(cr, x_offset, (line + 1) * settings.height - settings.font_size/2);
  cairo_set_source_rgb(cr, foreground->r, foreground->g, foreground->b);
  cairo_select_font_face(cr, settings.font_name, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, settings.font_size);
  cairo_show_text(cr, text);

  pthread_mutex_unlock(&global.draw_mutex);
}

static void draw_query_text(cairo_t *cr, cairo_surface_t *surface, const char *text, unsigned int cursor) {
  draw_typed_line(cr, (char *)text, 0, cursor, &settings.query_fg, &settings.query_bg);
  cairo_surface_flush(surface);
}

static void draw_response_text(xcb_connection_t *connection, xcb_window_t window, cairo_t *cr, cairo_surface_t *surface, result_t *results, unsigned int result_count) {

  if (window != 0) {
    unsigned int new_height = min(settings.height * (result_count + 1), settings.max_height);
    uint32_t values[] = { settings.width, new_height};

    xcb_configure_window (connection, window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
    cairo_xcb_surface_set_size(surface, settings.width, new_height);
  }

  int i;
  if (global.result_count - 1 < global.result_highlight) {
    global.result_highlight = global.result_count - 1;
  }
  for (i = 0; i < result_count; i++) {
    if (i != global.result_highlight) {
      draw_line(cr, results[i].text, i + 1, &settings.result_fg, &settings.result_bg);
    } else {
      draw_line(cr, results[i].text, i + 1, &settings.highlight_fg, &settings.highlight_bg);
    }
  }
  cairo_surface_flush(surface);
  xcb_flush(connection);
}

static void redraw_all(xcb_connection_t *connection, xcb_window_t window, cairo_t *cr, cairo_surface_t *surface, char *query_string, unsigned int query_cursor_index) {
  draw_query_text(cr, surface, query_string, query_cursor_index);
  draw_response_text(connection, window, cr, surface, global.results, global.result_count);
}

unsigned int parse_response_text(char *text, size_t length, result_t **results) {
  int index, mode;
  mode = 0; /* 0 -> closed, 1 -> opened no command, 2 -> opened, command */
  result_t *ret = calloc(1, sizeof(result_t));
  unsigned int count = 0;
  for (index = 0; text[index] != 0 && index < length; index++) {
    /* Escape sequence. */
    if (text[index] == '\\' && index + 1 < length) {
      switch (text[index+1]) {
        case '{':
        case '|':
        case '}':
          memmove(&text[index], &text[index+1], length - index);
          break;
        default:
          break;
      }
    }
    /* Opening brace. */
    else if (text[index] == '{') {
      if (mode != 0) {
        fprintf(stderr, "Syntax error, found { at index %d.\n %s\n", index, text);
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
        fprintf(stderr, "Syntax error, found | at index %d.\n %s\n", index, text);
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
        fprintf(stderr, "Syntax error, found } at index %d.\n %s\n", index, text);
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

  size_t res;

  while (1) {
    res = read(fd, global.result_buf, sizeof(global.result_buf));
    global.result_buf[res] = '\0';
    if (res <= 0) {
      fprintf(stderr, "Error in spawned cmd.\n");
      exit(1);
    }
    result_t *results;
    unsigned int result_count = parse_response_text(global.result_buf, res, &results);
    if (global.results && results != global.results) {
      free(global.results);
    }
    global.results = results;
    global.result_count = result_count;
    draw_response_text(connection, window, cairo_context, cairo_surface, results, result_count);
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

static inline int process_key_stroke(char *query_buffer, unsigned int *query_index, unsigned int *query_cursor_index, xcb_keysym_t key, xcb_connection_t *connection, cairo_t *cairo_context, cairo_surface_t *cairo_surface, FILE *to_write) {
  /* Check when we should update. */
  int redraw = 0;
  int resend = 0;

  switch (key) {
    case 65293: /* Enter. */
      if (global.results && global.result_highlight < global.result_count && global.result_highlight >= 0) {
        printf("%s", global.results[global.result_highlight].action);
        exit(0);
      }
      break;
    case 65361: /* Left. */
      if (*query_cursor_index > 0) {
        (*query_cursor_index)--;
        redraw = 1;
      }
      break;
    case 65363: /* Right. */
      if (*query_cursor_index < *query_index) {
        (*query_cursor_index)++;
        redraw = 1;
      }
      break;
    case 65362: /* Up. */
      if (global.result_highlight > 0) {
        global.result_highlight--;
        draw_response_text(connection, 0, cairo_context, cairo_surface, global.results, global.result_count);
      }
      break;
    case 65364: /* Down. */
      if (global.result_highlight < global.result_count-1) {
        global.result_highlight++;
        draw_response_text(connection, 0, cairo_context, cairo_surface, global.results, global.result_count);
      }
      break;
    case 65307: /* Escape. */
      return 0;
    case 65288: /* Backspace. */
      if (*query_index > 0 && *query_cursor_index > 0) {
        memmove(&query_buffer[(*query_cursor_index) - 1], &query_buffer[*query_cursor_index], *query_index - *query_cursor_index + 1);
        (*query_cursor_index)--;
        (*query_index)--;
        query_buffer[(*query_index)] = 0;
        redraw = 1;
        resend = 1;
      } else if (*query_index == 0) { /* Backspace with nothing? */
        return 0;
      }
      break;
    default:
      if (isprint(key) && *query_index < MAX_QUERY) {
        memmove(&query_buffer[(*query_cursor_index) + 1], &query_buffer[*query_cursor_index], *query_index - *query_cursor_index + 1);
        query_buffer[(*query_cursor_index)++] = key;
        (*query_index)++;
        redraw = 1;
        resend = 1;
      }
      break;
  }

  if (redraw) {
    draw_query_text(cairo_context, cairo_surface, query_buffer, *query_cursor_index);
    xcb_flush(connection);
  }

  if (resend) {
    if (write_to_remote(to_write, "%s\n", query_buffer)) {
      fprintf(stderr, "Failed to write.\n");
    }
  }

  return 1;
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

    wordexp_t expanded_file;
    wordexp(file, &expanded_file, 0);

    execlp((expanded_file.we_wordv)[0], (expanded_file.we_wordv)[0], NULL);
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

static void set_setting(char *param, char *val) {
  if (!strcmp("font_name", param)) {
    settings.font_name = val;
  } else if (!strcmp("font_size", param)) {
    sscanf(val, "%u", &settings.font_size);
  } else if (!strcmp("height", param)) {
    sscanf(val, "%u", &settings.height);
  } else if (!strcmp("width", param)) {
    sscanf(val, "%u", &settings.width);
  } else if (!strcmp("x", param)) {
    sscanf(val, "%u", &settings.x);
  } else if (!strcmp("y", param)) {
    sscanf(val, "%u", &settings.y);
  } else if (!strcmp("cmd", param)) {
    settings.cmd = val;
  } else if (!strcmp("query_fg", param)) {
    sscanf(val, "%f,%f,%f", &settings.query_fg.r, &settings.query_fg.g, &settings.query_fg.b);
  } else if (!strcmp("query_bg", param)) {
    sscanf(val, "%f,%f,%f", &settings.query_bg.r, &settings.query_bg.g, &settings.query_bg.b);
  } else if (!strcmp("result_fg", param)) {
    sscanf(val, "%f,%f,%f", &settings.result_fg.r, &settings.result_fg.g, &settings.result_fg.b);
  } else if (!strcmp("result_bg", param)) {
    sscanf(val, "%f,%f,%f", &settings.result_bg.r, &settings.result_bg.g, &settings.result_bg.b);
  } else if (!strcmp("highlight_fg", param)) {
    sscanf(val, "%f,%f,%f", &settings.highlight_fg.r, &settings.highlight_fg.g, &settings.highlight_fg.b);
  } else if (!strcmp("highlight_bg", param)) {
    sscanf(val, "%f,%f,%f", &settings.highlight_bg.r, &settings.highlight_bg.g, &settings.highlight_bg.b);
  }
}

static void initialize_settings(void) {
  /* Set default settings. */
  settings.query_fg.r = settings.result_fg.r = settings.highlight_fg.r = 0.1;
  settings.query_fg.g = settings.result_fg.g = settings.highlight_fg.g = 0.1;
  settings.query_fg.b = settings.result_fg.b = settings.highlight_fg.b = 0.1;
  settings.query_bg.r = settings.result_bg.r = settings.highlight_bg.r = 1.0;
  settings.query_bg.g = settings.result_bg.g = settings.highlight_bg.g = 1.0;
  settings.query_bg.b = settings.result_bg.b = settings.highlight_bg.b = 1.0;
  settings.font_size = 18;
  settings.max_height = 800;
  settings.height = 30;
  settings.width = 500;
  settings.x = 50;
  settings.y = 50;

  /* Read in from the config file. */
  if (chdir(getenv("HOME"))) {
    fprintf(stderr, "Unable to access the HOME directory.\n");
    exit(1);
  }

  size_t ret = 0;
  if (access(CONFIG_FILE, F_OK) != -1) {
    int fd = open(CONFIG_FILE, O_RDONLY);
    ret = read(fd, config_buf, sizeof(config_buf));
  } else {
    fprintf(stderr, "Couldn't open config file.\n");
  }
  int i, mode;
  mode = 1; /* 0 looking for param. 1 looking for value. 2 skipping chars */
  char *curr_param = config_buf;
  char *curr_val = NULL;
  for (i = 0; i < ret; i++) {
    switch (config_buf[i]) {
      case '=':
        config_buf[i] = '\0';
        mode = 1; /* Now we get the value. */
        break;
      case '\n':
        config_buf[i] = '\0';
        set_setting(curr_param, curr_val);
        mode = 0; /* Now we look for a new param. */
        break;
      default:
        if (mode == 0) {
          curr_param = &config_buf[i];
        } else if (mode == 1) {
          curr_val = &config_buf[i];
        }
        mode = 2;
        break;
    }
  (void)settings;
  }
}

int main(int argc, char **argv) {
  int exit_code = 0;
  initialize_settings();

  /* Set up the remote process. */
  int to_child_fd, from_child_fd;

  char *exec_file = settings.cmd;

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
            | XCB_EVENT_MASK_KEY_RELEASE
            | XCB_EVENT_MASK_BUTTON_PRESS;
  xcb_void_cookie_t window_cookie = xcb_create_window_checked(connection,
    XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, settings.width, settings.height, 0,
    XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
    XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values);

  if (check_xcb_cookie(window_cookie, connection, "Failed to initialize window.")) {
    exit_code = 1;
    goto cleanup;
  }

  /* Get the atoms to create a dock window type. */
  xcb_atom_t window_type_atom, window_type_dock_atom;
  xcb_intern_atom_cookie_t atom_cookie = xcb_intern_atom(connection, 0, strlen("_NET_WM_WINDOW_TYPE"), "_NET_WM_WINDOW_TYPE");
  xcb_intern_atom_reply_t *atom_reply = xcb_intern_atom_reply(connection, atom_cookie, NULL); 
  if (!atom_reply) {
    fprintf(stderr, "Unable to set window type. You will need to manually set your window manager to run lighthouse as you'd like.");
  } else {
    window_type_atom = atom_reply->atom;
    free(atom_reply);

    atom_cookie = xcb_intern_atom(connection, 0, strlen("_NET_WM_WINDOW_TYPE_DOCK"), "_NET_WM_WINDOW_TYPE_DOCK");
    atom_reply = xcb_intern_atom_reply(connection, atom_cookie, NULL);
    if (atom_reply) {
      window_type_dock_atom = atom_reply->atom;
      free(atom_reply);
      xcb_change_property_checked(connection, XCB_PROP_MODE_REPLACE, window, window_type_atom, XCB_ATOM_ATOM, 32, 1, &window_type_dock_atom);
    } else {
      fprintf(stderr, "Unable to set window type. You will need to manually set your window manager to run lighthouse as you'd like.");
    }
  }

  /* Set window properties. */
  char *title = "lighthouse";
  xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window,
    XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, strlen(title), title);
  xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window,
    XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, strlen(title), title);
  values[0] = settings.x * screen->width_in_pixels / 100 - settings.width / 2;
  values[1] = settings.y * screen->height_in_pixels / 100 - settings.height / 2;
  settings.screen_width = screen->width_in_pixels;
  settings.screen_height = screen->height_in_pixels;
  xcb_configure_window (connection, window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);

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
  cairo_surface_t *cairo_surface = cairo_xcb_surface_create(connection, window,visual, settings.width, settings.height);
  if (cairo_surface == NULL) {
    goto cleanup;
  }

  cairo_t *cairo_context = cairo_create(cairo_surface);
  if (cairo_context == NULL) {
    cairo_surface_destroy(cairo_surface);
    goto cleanup;
  }

  /* Spawn a thread to listen to our remote process. */
  if (pthread_mutex_init(&global.draw_mutex, NULL)) {
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

  xcb_map_window(connection, window);

  /* Query string. */
  char query_string[MAX_QUERY];
  memset(query_string, 0, sizeof(query_string));
  unsigned int query_index = 0;
  unsigned int query_cursor_index = 0;

  /* Now draw everything. */
  redraw_all(connection, window, cairo_context, cairo_surface, query_string, query_cursor_index);

  xcb_generic_event_t *event;
  while ((event = xcb_wait_for_event(connection))) {
    switch (event->response_type & ~0x80) {
      case XCB_EXPOSE: {
        /* Get the input focus. */
        xcb_void_cookie_t focus_cookie = xcb_set_input_focus_checked(connection, XCB_INPUT_FOCUS_POINTER_ROOT, window, XCB_CURRENT_TIME);
        check_xcb_cookie(focus_cookie, connection, "Failed to grab focus.");

        /* Redraw. */
        redraw_all(connection, window, cairo_context, cairo_surface, query_string, query_cursor_index);

        break;
      }
      case XCB_KEY_PRESS: {
        break;
      }
      case XCB_KEY_RELEASE: {
        xcb_key_release_event_t *k = (xcb_key_release_event_t *)event;
        xcb_keysym_t key = xcb_key_press_lookup_keysym(keysyms, k, k->state);
        int ret = process_key_stroke(query_string, &query_index, &query_cursor_index, key, connection, cairo_context, cairo_surface, to_child);
        if (ret <= 0) {
          exit_code = ret;
          goto cleanup;
        }
        break;
      }
      case XCB_EVENT_MASK_BUTTON_PRESS: {
        /* Get the input focus. */
        xcb_void_cookie_t focus_cookie = xcb_set_input_focus_checked(connection, XCB_INPUT_FOCUS_POINTER_ROOT, window, XCB_CURRENT_TIME);
        check_xcb_cookie(focus_cookie, connection, "Failed to grab focus.");
        break;
      }
      default:
        break;
    }

    free(event);
  }

  cairo_surface_destroy(cairo_surface);
  cairo_destroy(cairo_context);

cleanup:
  xcb_disconnect(connection);
  xcb_key_symbols_free(keysyms);

  return exit_code;
}

