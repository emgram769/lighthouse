/** @file lighthouse.c
 *  @author Bram Wasti <bwasti@cmu.edu>
 *
 *  @brief This file contains the main logic of lighthouse, a simple
 *         scriptable popup dialogue. See the README for information on usage.
 *
 *  @section LICENSE
 *
 *  Copyright (c) 2015 Bram Wasti
 *  Distributed under the MIT License.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 *
 */

#define _POSIX_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wordexp.h>
#include <xcb_keysyms.h>  /* xcb_key_symbols_alloc, xcb_key_press_lookup_keysym */

#include "child.h"
#include "display.h"
#include "globals.h"
#include "results.h"

/* declared in <string.h>, but not unless you define a suitable macro. Not sure which macro
   (see `man strdup`) is correct for this situation. */
extern char *strdup (const char *__s);

/** @brief Defaults for settings. */
#define HEIGHT            30
#define MAX_HEIGHT        7 * HEIGHT
#define WIDTH             500
#define FONT_SIZE         18
#define HALF_PERCENT      50
#define MAX_QUERY         1024
#define HORIZ_PADDING     5
#define CURSOR_PADDING    4

/* @brief Name of the file to search for. Directory appended at runtime. */
#define CONFIG_FILE       "/lighthouse/lighthouserc"


/* @brief Check the xcb cookie and prints an error if it has one.
 *
 * @param cookie A generic xcb cookie.
 * @param connection A connection to the Xorg server.
 * @param The error to be printed if the cookie contains an error.
 * @return
 */
static inline int32_t check_xcb_cookie(xcb_void_cookie_t cookie, xcb_connection_t *connection, char *error) {
  xcb_generic_error_t *xcb_error = xcb_request_check(connection, cookie);
  if (xcb_error) {
    fprintf(stderr, "[error:%"PRIu8"] %s\n", xcb_error->error_code, error);
    return 1;
  }
  return 0;
}

/* @brief Return number of modifiers present in mask
 *    0: "Nothing"
 *    1: "Shift"
 *    2: "Lock"
 *    3: "Ctrl"
 *    4: "Alt",
 *    5: "Mod2"
 *    6: "Mod3"
 *    7: "Mod4"
 *    8: "Mod5",
 *    9: "Button1"
 *    10: "Button2"
 *    11: "Button3"
 *    12: "Button4"
 *    13: "Button5"
 *    (http://xcb.freedesktop.org/tutorial/events/)
 *
 * @param mask Modifier mask return by
 *      xcb_key_release_event_t k ... ->event;
 * */
uint8_t get_modifiers (uint32_t mask) {
    uint8_t modifier = 0;
    if (mask) {
        ++modifier;
        while (!(mask & 1)) {
            mask >>= 1;
            ++modifier;
        }
    }
    return modifier;
}


/* @brief Set the param "highlight" on the next line by passing all
 *        the title line (with no action).
 *
 * @param Copy of the global.result_highlight for the ease of use.
 */
static void get_next_non_title(uint32_t *highlight) {
    (*highlight)++;
    while ((*highlight) < global.result_count && !global.results[*highlight].action) {
        /* Searching for the next result with an action.*/
        (*highlight)++;
    }
}

/* @brief Set the global.result_highlight on the next line
 *        that can be clicked (contain an action), and take care of
 *        the bottom of the window.
 *
 * @param Copy of the global.result_highlight for the ease of use.
 */
static void get_next_line(uint32_t *highlight) {
      get_next_non_title(highlight);
      if(*highlight == global.result_count) {
          /* If the last result is a title go on the top. */
          *highlight = -1;
          global.result_offset = 0;
          get_next_non_title(highlight);
      }
      global.result_highlight = *highlight;
}

/* @brief Set the param "highlight" on the previous line by passing all
 *        the title line (with no action).
 *
 * @param Copy of the global.result_highlight for the ease of use.
 */
static void get_previous_non_title(uint32_t *highlight) {
    (*highlight)--;
    while ((*highlight) < global.result_count && !global.results[*highlight].action) {
        /* Searching for the previous result with an action.
         *
         * *(*highlight) < global.result_count is used because I use highlight is
         * unsigned so it when it hit "-1", it's bigger than global.result_count
         */
        (*highlight)--;
    }
}

/* @brief Set the global.result_highlight on the previous line
 *        that can be clicked (contain an action), and take care of
 *        the top of the window.
 *
 * @param Copy of the global.result_highlight for the ease of use.
 */
static void get_previous_line(uint32_t *highlight) {
    get_previous_non_title(highlight);

    if(*highlight == (uint32_t) - 1) {
        *highlight = global.result_count;
        get_previous_non_title(highlight);
    }
    global.result_highlight = *highlight;
}


/* @brief Processes an entered key by:
 *
 * 1) Adding the key to the query buffer (backspace will remove a character).
 * 2) Drawing the updated query to the screen if necessary.
 * 3) Writing the updated query to the child process if necessary.
 *
 * @param query_buffer The string of the current query (what is typed).
 * @param query_index A reference to the current length of the query.
 * @param query_cursor_index A reference to the current index of the cursor
          in the query.
 * @param key The key enetered.
 * @param connection A connection to the Xorg server.
 * @param cairo_context A cairo context for drawing to the screen.
 * @param cairo_surface A cairo surface for drawing to the screen.
 * @param to_write A descriptor to write to the child process.
 * @return 0 on success and 1 on failure.
 */
static inline int32_t process_key_stroke(xcb_window_t window, char *query_buffer, uint32_t *query_index, uint32_t *query_cursor_index, xcb_keysym_t key, uint16_t modifier_mask, xcb_connection_t *connection, cairo_t *cairo_context, cairo_surface_t *cairo_surface, FILE *to_write) {
  pthread_mutex_lock(&global.result_mutex);

  /* Check when we should update. */
  int32_t redraw = 0;
  int32_t resend = 0;

  uint8_t mod_key = get_modifiers(modifier_mask);

  debug("key: %u, modifier: %u\n", key, mod_key);

  uint32_t highlight = global.result_highlight;
  uint32_t old_pos;
  if (global.result_count && key == 100 && mod_key == 3) {
      /* CTRL-D
       * GO down to the next title
       */
      while (highlight < global.result_count && global.results[highlight].action) {
          highlight++;
      }
      if (highlight == global.result_count) {
        /* highlight hit the bottom. */
        highlight = 0;
        global.result_offset = 0;
        while (highlight < global.result_count - 1 && global.results[highlight].action) {
            highlight++;
        }
      }
      get_next_line(&highlight);
      draw_result_text(connection, window, cairo_context, cairo_surface, global.results);
  } else if (global.result_count && key == 117 && mod_key == 3) {
      /* CTRL-U
       * GO up to the next title
       */
      while (highlight > 0 && global.results[highlight].action) {
          highlight--;
      }

      if (highlight == 0 && global.results[highlight].action) {
        /* highlight hit the top . */
        highlight = global.result_count - 1;
        while (highlight > 0 && global.results[highlight].action) {
            highlight--;
        }
      }
      get_previous_line(&highlight);
      draw_result_text(connection, window, cairo_context, cairo_surface, global.results);
  } else {
  switch (key) {
    case 65293: /* Enter. */
      if (global.results && global.result_highlight < global.result_count) {
        printf("%s", global.results[global.result_highlight].action);
        goto cleanup;
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
      if (!global.result_count)
          break;
      if (highlight) { /* Avoid segfault when highlight on the top. */
        old_pos = highlight;
        get_previous_non_title(&highlight);
        if (!global.results[highlight].action) {
            /* If it's a title it mean the get_previous_non_title function
            * found nothing and hit the top.
            */
            highlight = old_pos; /* To not let the highlight point on a title. */
            if (global.result_offset)
                global.result_offset--;
        }
        global.result_highlight = highlight;
        draw_result_text(connection, window, cairo_context, cairo_surface, global.results);
      }
      break;
    case 65364: /* Down. */
      if (!global.result_count)
          break;
      if (highlight < global.result_count - 1) {
       old_pos = highlight;
       get_next_non_title(&highlight);
       if (highlight == global.result_count) {
           /* If no other result with an action can be found, it just inc the
            * the offset so it can show the hidden title and make the highlight to the
            * previous non_title.
            * NB: If the offset limit is exceed, it's handled by the draw_result_text function.
            */
            highlight = old_pos;
            global.result_offset++;
       }
       global.result_highlight = highlight;
       draw_result_text(connection, window, cairo_context, cairo_surface, global.results);
      }
      break;
    case 65289: /* Tab. */
      if (!global.result_count)
          break;
      get_next_line(&highlight);
      draw_result_text(connection, window, cairo_context, cairo_surface, global.results);
      break;
    case 65056: /* Shift Tab */
      if (!global.result_count)
          break;
      get_previous_line(&highlight);
      draw_result_text(connection, window, cairo_context, cairo_surface, global.results);
      break;
    case 65307: /* Escape. */
      goto cleanup;
    case 65288: /* Backspace. */
      if (*query_index > 0 && *query_cursor_index > 0) {
          memmove(&query_buffer[(*query_cursor_index) - 1], &query_buffer[*query_cursor_index], *query_index - *query_cursor_index + 1);
          (*query_cursor_index)--;
          (*query_index)--;
          query_buffer[(*query_index)] = 0;
          redraw = 1;
          resend = 1;
      } else if (*query_index == 0 && settings.backspace_exit) { /* Backspace with nothing */
          goto cleanup;
      }
      break;
    default:
      if (isprint((char)key) && *query_index < MAX_QUERY) {
          memmove(&query_buffer[(*query_cursor_index) + 1], &query_buffer[*query_cursor_index], *query_index - *query_cursor_index + 1);
          query_buffer[(*query_cursor_index)++] = key;
          (*query_index)++;
          redraw = 1;
          resend = 1;
      }
      break;
  }
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

  pthread_mutex_unlock(&global.result_mutex);
  return 1;

cleanup:
  pthread_mutex_unlock(&global.result_mutex);
  return 0;
}

/* @brief Updates the settings global struct with the passed in parameters.
 *
 * @param param The name of the parameter to be updated.
 * @param val The value of the parameter to be updated.
 * @return Void.
 */
static void set_setting(char *param, char *val) {
  if (!strcmp("font_name", param)) {
    settings.font_name = val;
  } else if (!strcmp("font_size", param)) {
    sscanf(val, "%u", &settings.font_size);
  } else if (!strcmp("horiz_padding", param)) {
    sscanf(val, "%u", &settings.horiz_padding);
  } else if (!strcmp("cursor_padding", param)) {
    sscanf(val, "%u", &settings.cursor_padding);
  } else if (!strcmp("cursor_is_underline", param)) {
    sscanf(val, "%d", &settings.cursor_is_underline);
  } else if (!strcmp("height", param)) {
    sscanf(val, "%u", &settings.height);
  } else if (!strcmp("width", param)) {
    sscanf(val, "%u", &settings.width);
  } else if (!strcmp("x", param)) {
    sscanf(val, "%u", &settings.x);
  } else if (!strcmp("y", param)) {
    sscanf(val, "%u", &settings.y);
  } else if (!strcmp("max_height", param)) {
    sscanf(val, "%u", &settings.max_height);
  } else if (!strcmp("screen", param)) {
    sscanf(val, "%u", &settings.screen);
  } else if (!strcmp("backspace_exit", param)) {
    sscanf(val, "%d", &settings.backspace_exit);
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
  } else if (!strcmp("desktop", param)) {
    sscanf(val, "%u", &settings.desktop);
  } else if (!strcmp("dock_mode", param)) {
    sscanf(val, "%u", &settings.dock_mode);
  } else if (!strcmp("desc_size", param)) {
    sscanf(val, "%u", &settings.desc_size);
  } else if (!strcmp("auto_center", param)) {
    sscanf(val, "%u", &settings.auto_center);
  } else if (!strcmp("line_gap", param)) {
    sscanf(val, "%u", &settings.line_gap);
  } else if (!strcmp("desc_font_size", param)) {
    sscanf(val, "%u", &settings.desc_font_size);
  }
}

/* @brief Attempts to find secondary displays and updates settings.screen_* data
 *        with the dimensions of the found screens.
 *
 * Note: failure is somewhat expected and is handled by simply using the default
 *       xcb screen's dimension parameters.
 *
 * @param connection A connection to the Xorg server.
 * @param screen A screen created by xcb's xcb_setup_roots function.
 * @return 0 on success and 1 on failure.
 */
static int32_t get_multiscreen_settings(xcb_connection_t *connection, xcb_screen_t *screen) {
  /* First check randr. */
  const xcb_query_extension_reply_t *extension_reply = xcb_get_extension_data(connection, &xcb_randr_id);
  if (extension_reply && extension_reply->present) {
    debug("Found randr support, searching for displays.\n");
    /* Find x, y and width, height. */
    xcb_randr_get_screen_resources_current_reply_t *randr_reply = xcb_randr_get_screen_resources_current_reply(connection, xcb_randr_get_screen_resources_current(connection, screen->root), NULL);
    if (!randr_reply) {
      fprintf(stderr, "Failed to get randr set up.\n");
    } else {
      int32_t num_outputs = xcb_randr_get_screen_resources_current_outputs_length(randr_reply);
      if (num_outputs < settings.screen) {
        fprintf(stderr, "Screen selected not found.\n");
        /* Default back to the first screen. */
        settings.screen = 0;
      }
      xcb_randr_output_t *outputs = xcb_randr_get_screen_resources_current_outputs(randr_reply);
      uint32_t output_index = settings.screen;
      xcb_randr_get_output_info_reply_t *randr_output = NULL;
      do {
        if (randr_output) { free(randr_output); }
        randr_output = xcb_randr_get_output_info_reply(connection, xcb_randr_get_output_info(connection, outputs[output_index], XCB_CURRENT_TIME), NULL);
        output_index++;
      } while (randr_output && (randr_output->connection != XCB_RANDR_CONNECTION_CONNECTED) && (output_index < num_outputs));
      if (randr_output) {
        xcb_randr_get_crtc_info_reply_t *randr_crtc = xcb_randr_get_crtc_info_reply(connection, xcb_randr_get_crtc_info(connection, randr_output->crtc, XCB_CURRENT_TIME), NULL);
        if (!randr_crtc) {
          fprintf(stderr, "Unable to connect to randr crtc\n");
          free(randr_output);
          free(randr_reply);
          goto xinerama;
        }
        settings.screen_width = randr_crtc->width;
        settings.screen_height = randr_crtc->height;
        settings.screen_x = randr_crtc->x;
        settings.screen_y = randr_crtc->y;
        debug("randr screen initialization successful, x: %u y: %u w: %u h: %u.\n", settings.screen_x, settings.screen_y, settings.screen_width, settings.screen_height);

        free(randr_crtc);
        free(randr_output);
        free(randr_reply);
        return 0;
      }
      free(randr_output);
      free(randr_reply);
    }
  }
xinerama:
  debug("Did not find randr support, attempting xinerama\n");

  /* Still here? Let's try xinerama! */
  extension_reply = xcb_get_extension_data(connection, &xcb_xinerama_id);
  if (extension_reply && extension_reply->present) {
    debug("Found xinerama support, searching for displays.\n");
    xcb_xinerama_is_active_reply_t *xinerama_is_active_reply = xcb_xinerama_is_active_reply(connection, xcb_xinerama_is_active(connection), NULL);
    if (xinerama_is_active_reply && xinerama_is_active_reply->state) {
      free(xinerama_is_active_reply);
      /* Find x, y and width, height. */
      xcb_xinerama_query_screens_reply_t *screen_reply = xcb_xinerama_query_screens_reply(connection, xcb_xinerama_query_screens_unchecked(connection), NULL);
      xcb_xinerama_screen_info_iterator_t iter = xcb_xinerama_query_screens_screen_info_iterator(screen_reply);
      free(screen_reply);
      if (iter.rem < settings.screen) {
        fprintf(stderr, "Screen selected not found.\n");
        /* Default back to the first screen. */
        settings.screen = 0;
      }
      /* Jump to the appropriate screen. */
      int32_t i = 0;
      while (i < settings.screen) {
        xcb_xinerama_screen_info_next(&iter);
        i++;
      }
      settings.screen_width = iter.data->width;
      settings.screen_height = iter.data->height;
      settings.screen_x = iter.data->x_org;
      settings.screen_y = iter.data->y_org;
      debug("xinerama screen initialization successful, x: %u y: %u w: %u h: %u.\n", settings.screen_x, settings.screen_y, settings.screen_width, settings.screen_height);
      return 0;
    }
  }

  debug("Multiscreen search failed.\n");
  return 1;
}

/* @brief Initializes the settings global structure and read in the configuration
 *        file.
 *
 * Note: this function does not initialize settings.screen_* data.
 *
 * @return Void.
 */
static int initialize_settings(char *config_file) {
  /* Set default settings. */
  settings.query_fg.r = settings.highlight_fg.r = 0.1;
  settings.query_fg.g = settings.highlight_fg.g = 0.1;
  settings.query_fg.b = settings.highlight_fg.b = 0.1;
  settings.result_fg.r = 0.5;
  settings.result_fg.g = 0.5;
  settings.result_fg.b = 0.5;
  settings.query_bg.r = settings.result_bg.r = settings.highlight_bg.r = 1.0;
  settings.query_bg.g = settings.result_bg.g = settings.highlight_bg.g = 1.0;
  settings.query_bg.b = settings.result_bg.b = settings.highlight_bg.b = 1.0;
  settings.font_size = FONT_SIZE;
  settings.horiz_padding = HORIZ_PADDING;
  settings.cursor_padding = CURSOR_PADDING;
  settings.max_height = MAX_HEIGHT;
  settings.height = HEIGHT;
  settings.width = WIDTH;
  settings.x = HALF_PERCENT;
  settings.y = HALF_PERCENT;
  settings.desktop = 0xFFFFFFFF;
  settings.screen = 0;
  settings.backspace_exit = 1;
  settings.dock_mode = 1;
  settings.desc_size = 300;
  settings.auto_center = 1;
  settings.line_gap = 20;
  settings.desc_font_size = FONT_SIZE;

  /* Read in from the config file. */
  wordexp_t expanded_file;
  if (wordexp(config_file, &expanded_file, 0)) {
    fprintf(stderr, "Error expanding file %s\n", config_file);
  } else {
    config_file = expanded_file.we_wordv[0];
  }

  size_t ret = 0;
  int32_t fd = open(config_file, O_RDONLY);
  if (fd == -1) {
    fprintf(stderr, "Couldn't open config file %s: %s\n", config_file, strerror(errno));
    return 1;
  } else {
    ret = read(fd, global.config_buf, sizeof(global.config_buf));
  }

  int32_t i, mode;
  mode = 1; /* 0 looking for param. 1 looking for value. 2 skipping chars */
  char *curr_param = global.config_buf;
  char *curr_val = NULL;
  for (i = 0; i < ret; i++) {
    switch (global.config_buf[i]) {
      case '=':
        global.config_buf[i] = '\0';
        mode = 1; /* Now we get the value. */
        break;
      case '\n':
        global.config_buf[i] = '\0';
        set_setting(curr_param, curr_val);
        mode = 0; /* Now we look for a new param. */
        break;
      default:
        if (mode == 0) {
          curr_param = &global.config_buf[i];
        } else if (mode == 1) {
          curr_val = &global.config_buf[i];
        }
        mode = 2;
        break;
    }
  }
  return 0;
}

/* @brief A function called at the end of execution to clean up
 *        the spawned child process.
 *
 * @return Void.
 */
void kill_zombie(void) {
  kill(global.child_pid, SIGTERM);
  while(wait(NULL) == -1);
}



/* @brief The main function. Initialization happens here.
 *
 * @return 0 on success and 1 on failure.
 */
int main(int argc, char **argv) {
  int32_t exit_code = 0;
  atexit(kill_zombie);

  /* Determine if there is an XDG_CONFIG_HOME to put the config in, otherwise use ~/.config */
  char *config_file_dir = (getenv("XDG_CONFIG_HOME")) ? getenv("XDG_CONFIG_HOME") : "~/.config";
  char *config_file = malloc(strlen(config_file_dir) + strlen(CONFIG_FILE) + 1);
  if (!config_file) {
    return 1;
  }
  sprintf(config_file, "%s%s", config_file_dir, CONFIG_FILE);
  int c;
  while ((c = getopt(argc, argv, "c:")) != -1) {
    switch (c) {
      case 'c':
        config_file = strdup(optarg);
        break;
      default:
        break;
    }
  }

  if (initialize_settings(config_file)) {
    free(config_file);
    return 1;
  }
  free(config_file);

  int i;
  enum { MAX_ARGS = 64 };
  int nargs = 0;
  char *cmdargs[MAX_ARGS];

  /* one extra for the NULL */
  nargs = (argc - optind) + 2;
  if (nargs > 63)
    nargs = 63;

  for (i=optind; i < argc; i++) {
    if ((i - optind) + 1 > 62)
      break;
    cmdargs[(i - optind) + 1] = strdup(argv[i]);
  }

  cmdargs[nargs - 1] = NULL;

  /* Set up the remote process. */
  int32_t to_child_fd, from_child_fd;

  char *exec_file = settings.cmd;

  if (spawn_piped_process(exec_file, &to_child_fd, &from_child_fd, (char **)cmdargs)) {
    fprintf(stderr, "Failed to spawn piped process.\n");
    exit_code = 1;
    return exit_code;
  }

  /* Don't free #0, it is filled in by spawn_piped_process() and isn't memory that we own */
  for (i=1; i < nargs - 1 ; i++)
    free(cmdargs[i]);

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

	/* Override-redirect to inform a window manager not to tamper with the window. */
	window_cookie = xcb_change_window_attributes(connection, window, XCB_CW_OVERRIDE_REDIRECT, values);
  if (check_xcb_cookie(window_cookie, connection, "Failed to override window redirect.")) {
    exit_code = 1;
    goto cleanup;
  }

  /* Get the atoms to create a dock window type. */
  xcb_atom_t window_type_atom, window_type_dock_atom;
  xcb_intern_atom_cookie_t atom_cookie = xcb_intern_atom(connection, 0, strlen("_NET_WM_WINDOW_TYPE"), "_NET_WM_WINDOW_TYPE");
  xcb_intern_atom_reply_t *atom_reply = xcb_intern_atom_reply(connection, atom_cookie, NULL);
  if (!atom_reply) {
    fprintf(stderr, "Unable to set window type. You will need to manually set your window manager to run lighthouse as you'd like.\n");
  } else {
    window_type_atom = atom_reply->atom;
    free(atom_reply);

    if (settings.dock_mode) {
        atom_cookie = xcb_intern_atom(connection, 0, strlen("_NET_WM_WINDOW_TYPE_DOCK"), "_NET_WM_WINDOW_TYPE_DOCK");
    } else {
        atom_cookie = xcb_intern_atom(connection, 0, strlen("_NET_WM_WINDOW_TYPE_DIALOG"), "_NET_WM_WINDOW_TYPE_DIALOG");
    }
    atom_reply = xcb_intern_atom_reply(connection, atom_cookie, NULL);
    if (atom_reply) {
      window_type_dock_atom = atom_reply->atom;
      free(atom_reply);
      xcb_change_property_checked(connection, XCB_PROP_MODE_REPLACE, window, window_type_atom, XCB_ATOM_ATOM, 32, 1, &window_type_dock_atom);
    } else {
      fprintf(stderr, "Unable to set window type. You will need to manually set your window manager to run lighthouse as you'd like.\n");
    }
  }

  /* Now set which desktop to run on. */
  xcb_atom_t desktop_atom;
  atom_cookie = xcb_intern_atom(connection, 0, strlen("_NET_WM_DESKTOP"), "_NET_WM_DESKTOP");
  atom_reply = xcb_intern_atom_reply(connection, atom_cookie, NULL);
  if (!atom_reply) {
    fprintf(stderr, "Unable to set a specific desktop to launch on.\n");
  } else {
    desktop_atom = atom_reply->atom;
    free(atom_reply);
    xcb_change_property_checked(connection, XCB_PROP_MODE_REPLACE, window, desktop_atom, XCB_ATOM_ATOM, 32, 1, (const uint32_t []){ settings.desktop });
  }

  /* Demand attention. */
  xcb_atom_t state_atom, attention_state_atom;
  atom_cookie = xcb_intern_atom(connection, 0, strlen("_NET_WM_STATE"), "_NET_WM_STATE");
  atom_reply = xcb_intern_atom_reply(connection, atom_cookie, NULL);
  if (!atom_reply) {
    fprintf(stderr, "Unable to grab desktop attention.\n");
  } else {
    state_atom = atom_reply->atom;
    free(atom_reply);
    atom_cookie = xcb_intern_atom(connection, 0, strlen("_NET_WM_STATE_DEMANDS_ATTENTION"), "_NET_WM_STATE_DEMANDS_ATTENTION");
    atom_reply = xcb_intern_atom_reply(connection, atom_cookie, NULL);
    if (!atom_reply) {
      fprintf(stderr, "Unable to grab desktop attention.\n");
    } else {
      attention_state_atom = atom_reply->atom;
      free(atom_reply);
      xcb_change_property_checked(connection, XCB_PROP_MODE_REPLACE, window, state_atom, XCB_ATOM_ATOM, 32, 1, &attention_state_atom);
    }
  }

  /* Get multiscreen information or default to the screen properties. */
  if (get_multiscreen_settings(connection, screen)) {
    settings.screen_width = screen->width_in_pixels;
    settings.screen_height = screen->height_in_pixels;
    settings.screen_x = 0;
    settings.screen_y = 0;
  }

  /* Set window properties. */
  char *title = "lighthouse";
  xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window,
    XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, strlen(title), title);
  xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window,
    XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, strlen(title), title);

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

  /* Getting the recommended free space for the font see
   * http://cairographics.org/manual/cairo-cairo-scaled-font-t.html#cairo-font-extents-t
   * for more information. */
  cairo_select_font_face(cairo_context, settings.font_name, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cairo_context, settings.font_size);
  cairo_font_extents_t extents;
  cairo_font_extents(cairo_context, &extents);
  global.real_font_size = extents.height;

  cairo_set_font_size(cairo_context, settings.desc_font_size);
  cairo_font_extents(cairo_context, &extents);
  global.real_desc_font_size = extents.height;
  printf("%u to %f\n", settings.desc_font_size, global.real_desc_font_size);

  /* Spawn a thread to listen to our remote process. */
  if (pthread_mutex_init(&global.draw_mutex, NULL)) {
    fprintf(stderr, "Failed to create mutex.");
    goto cleanup;
  }

  if (pthread_mutex_init(&global.result_mutex, NULL)) {
    fprintf(stderr, "Failed to create mutex.");
    goto cleanup;
  }

  struct result_params results_thr_params;
  results_thr_params.fd = from_child_fd;
  results_thr_params.cr = cairo_context;
  results_thr_params.cr_surface = cairo_surface;
  results_thr_params.connection = connection;
  results_thr_params.window = window;

  if (pthread_create(&global.results_thr, NULL, &get_results, &results_thr_params)) {
    fprintf(stderr, "Couldn't spawn second thread: %s\n", strerror(errno));
    exit(1);
  }

  xcb_map_window(connection, window);

  /* Query string. */
  char query_string[MAX_QUERY];
  memset(query_string, 0, sizeof(query_string));
  uint32_t query_index = 0;
  uint32_t query_cursor_index = 0;

  /* Now draw everything. */
  cairo_set_line_width(cairo_context, 2);
  redraw_all(connection, window, cairo_context, cairo_surface, query_string, query_cursor_index);

  /* and center it */
  /* Assign value for the window position with and without description window */
  global.win_x_pos_with_desc = settings.screen_x + settings.x * settings.screen_width / 100
      - (settings.width + settings.desc_size) / 2;
  global.win_x_pos = settings.screen_x + settings.x * settings.screen_width / 100 - settings.width / 2;
  global.win_y_pos  = settings.screen_y + settings.y * settings.screen_height / 100 - settings.height / 2;

  if (settings.auto_center) {
    values[0] = global.win_x_pos;
    values[1] = global.win_y_pos;
  } else {
    values[0] = global.win_x_pos_with_desc;
    values[1] = global.win_y_pos;
  }
  xcb_configure_window(connection, window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);

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
        xcb_keysym_t key = xcb_key_press_lookup_keysym(keysyms, k, k->state & ~XCB_MOD_MASK_2 & ~XCB_MOD_MASK_CONTROL);
        int32_t ret = process_key_stroke(window, query_string, &query_index, &query_cursor_index, key, k->state, connection, cairo_context, cairo_surface, to_child);
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
