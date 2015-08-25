/** @file lighthouse.c
 *  @author Bram Wasti <bwasti@cmu.edu>
 *
 *  @brief This file contains the implementation of lighthouse, a simple
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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <inttypes.h>
#include <wordexp.h>

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <getopt.h>

#include <xcb/xcb.h>
#include <xcb/xinerama.h>
#include <xcb/randr.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>
#include <pthread.h>
#include <errno.h>

#include <xcb_keysyms.h>  /* xcb_key_symbols_alloc, xcb_key_press_lookup_keysym */

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

/* @brief Size of the buffers. */
#define MAX_CONFIG_SIZE   10 * 1024
#define MAX_RESULT_SIZE   10 * 1024

/* @brief Name of the file to search for. Directory appended at runtime. */
#define CONFIG_FILE       "/lighthouse/lighthouserc"

#define min(a,b) ((a) < (b) ? (a) : (b))

#ifdef DEBUG
#define debug(...) fprintf(stdout, __VA_ARGS__)
#else
#define debug(...) (void)0
#endif

/* @brief Type used to pass around cairo color information conveniently. */
typedef struct {
  float r;
  float g;
  float b;
} color_t;

/* @brief Type used to pass around x,y offsets. */
typedef struct {
  uint32_t x;
  uint32_t y;
  uint32_t image_y;
} offset_t;

/* @brief Type used to maintain a list of results in a usable form. */
typedef struct {
  char *text;
  char *action;
  char *desc;
} result_t;

/* @brief The type of data to draw. */
typedef enum {
  DRAW_TEXT,
  DRAW_IMAGE,
  BOLD,
  NEW_LINE
} draw_type_t;

/* @brief Type used to pass around drawing options. */
typedef struct {
  draw_type_t type;
  char *data;
} draw_t;

/* @brief This struct is exclusively used to spawn a thread. */
struct result_params {
  cairo_t *cr;
  cairo_surface_t *cr_surface;
  xcb_connection_t *connection;
  xcb_window_t window;
  int32_t fd;
};

/* @brief A struct of globals that are used throughout the program. */
static struct {
  pthread_mutex_t draw_mutex;
  pthread_mutex_t result_mutex;
  char result_buf[MAX_RESULT_SIZE];
  result_t *results;
  char config_buf[MAX_CONFIG_SIZE];
  uint32_t result_count;
  uint32_t result_highlight;
  uint32_t result_offset;
  int32_t child_pid;
  pthread_t results_thr;
  uint32_t win_x_pos;
  uint32_t win_x_pos_with_desc;
  uint32_t win_y_pos;
  double real_font_size;
} global;

/* @brief A struct of settings that are set and used when the program starts. */
static struct {
  /* The color scheme. */
  color_t query_fg;
  color_t query_bg;
  color_t result_fg;
  color_t result_bg;
  color_t highlight_fg;
  color_t highlight_bg;

  /* The process to pipe input to. */
  char *cmd;

  /* Options. */
  int backspace_exit;

  /* Font. */
  char *font_name;
  uint32_t font_size;
  uint32_t horiz_padding;
  uint32_t cursor_padding;
  int cursor_is_underline;

  /* Size. */
  uint32_t height;
  uint32_t max_height;
  uint32_t width;
  /* Percentage offset ont the screen. */
  uint32_t x;
  uint32_t y;

  /* For multiple display. */
  uint32_t screen;
  uint32_t screen_x;
  uint32_t screen_y;
  uint32_t screen_height;
  uint32_t screen_width;

  /* Which desktop to run on. */
  uint32_t desktop;

  /* Set to 1 to use _NET_WM_WINDOW_TYPE_DOCK
   * Set to 0 to use _NET_WM_WINDOW_TYPE_DIALOG (for i3 users)
   */
  uint32_t dock_mode;

  /* Description option */
  uint32_t desc_size;
  uint32_t auto_center;
} settings;

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

/* @brief Returns the offset for a line of text.
 *
 * @param line the index of the line to be drawn (counting from the top).
 * @return the line's offset.
 */
static inline offset_t calculate_line_offset(uint32_t line) {
    offset_t result;
    result.x  = settings.horiz_padding;
    result.y  = settings.height * line + global.real_font_size;
    result.image_y = settings.height * (line + 1);

    return result;
}

/* @brief Draw a line of text with a cursor to a cairo context.
 *
 * @param cr A cairo context for drawing to the screen.
 * @param text The text to be drawn.
 * @param line The index of the line to be drawn (counting from the top).
 * @param cursor The index of the cursor into the text to be drawn.
 * @param foreground The color of the text.
 * @param background The color of the background.
 * @return Void.
 */
static void draw_typed_line(cairo_t *cr, char *text, uint32_t line, uint32_t cursor, color_t *foreground, color_t *background) {
  pthread_mutex_lock(&global.draw_mutex);

  /* Set the background. */
  cairo_set_source_rgb(cr, background->r, background->g, background->b);
  cairo_rectangle(cr, 0, line * settings.height, settings.width, (line + 1) * settings.height);
  cairo_stroke_preserve(cr);
  cairo_fill(cr);

  /* Set the foreground color and font. */
  cairo_set_source_rgb(cr, foreground->r, foreground->g, foreground->b);
  cairo_select_font_face(cr, settings.font_name, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

  offset_t offset = calculate_line_offset(line);
  /* Find the cursor relative to the text. */
  cairo_text_extents_t extents;
  char saved_char = text[cursor];
  text[cursor] = '\0';
  cairo_text_extents(cr, text, &extents);
  text[cursor] = saved_char;
  int32_t cursor_x = extents.x_advance;

  /* Find the text offset. */
  cairo_text_extents(cr, text, &extents);
  if (settings.width < extents.width) {
    offset.x = settings.width - extents.x_advance;
  }

  cursor_x += offset.x;

  /* if the cursor would be off the back end, set its position to 0 and scroll text instead */
  if(cursor_x < 0) {
      offset.x -= (cursor_x-3);
      cursor_x = 0;
  }

  /* Draw the text. */
  cairo_move_to(cr, offset.x, offset.y);
  cairo_set_font_size(cr, settings.font_size);
  cairo_show_text(cr, text);

  /* Draw the cursor. */
  if (settings.cursor_is_underline) {
    cairo_show_text(cr, "_");
  } else {
    uint32_t cursor_y = offset.y - settings.font_size - settings.cursor_padding;
    cairo_set_source_rgb(cr, foreground->r, foreground->g, foreground->b);
    cairo_rectangle(cr, cursor_x + 2, cursor_y, 0, settings.font_size + (settings.cursor_padding * 2));
    cairo_stroke_preserve(cr);
    cairo_fill(cr);
  }

  pthread_mutex_unlock(&global.draw_mutex);
}

/* @brief Draw text at the given offset.
 *
 * @param cr A cairo context for drawing to the screen.
 * @param text The text to be drawn.
 * @param foreground The color of the text.
 * @return The advance in the x direction.
 */
static uint32_t draw_text(cairo_t *cr, const char *text, offset_t offset, color_t *foreground, cairo_font_weight_t weight) {
  cairo_text_extents_t extents;
  cairo_text_extents(cr, text, &extents);
  cairo_move_to(cr, offset.x, offset.y);
  cairo_set_source_rgb(cr, foreground->r, foreground->g, foreground->b);
  cairo_select_font_face(cr, settings.font_name, CAIRO_FONT_SLANT_NORMAL, weight);
  cairo_set_font_size(cr, settings.font_size);
  cairo_show_text(cr, text);
  return extents.x_advance;
}

cairo_surface_t * scale_surface (cairo_surface_t *surface, int width, int height,
        int new_width, int new_height) {
    cairo_surface_t *new_surface = cairo_surface_create_similar(surface,
            CAIRO_CONTENT_COLOR_ALPHA, new_width, new_height);
    cairo_t *cr = cairo_create (new_surface);

    cairo_scale (cr, (double)new_width / width, (double)new_height / height);
    cairo_set_source_surface (cr, surface, 0, 0);

    cairo_pattern_set_extend (cairo_get_source(cr), CAIRO_EXTEND_REFLECT);
    cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);

    cairo_paint (cr);

    cairo_destroy (cr);

    return new_surface;
}

/* @brief Draw an image at the given offset.
 *
 * @param cr A cairo context for drawing to the screen.
 * @param file The image to be drawn.
 * @return The advance in the x direction.
 */
static uint32_t draw_image(cairo_t *cr, const char *file, offset_t offset) {
  // TODO CHange parameter to know the size of the window.
  wordexp_t expanded_file;
  if (wordexp(file, &expanded_file, 0)) {
    fprintf(stderr, "Error expanding file %s\n", file);
  } else {
    file = expanded_file.we_wordv[0];
  }

  if (access(file, F_OK) == -1) {
    fprintf(stderr, "Cannot open image file %s\n", file);
    return 0;
  }

  cairo_surface_t *img;
  img = cairo_image_surface_create_from_png(file);
  int w = cairo_image_surface_get_width(img);
  int h = cairo_image_surface_get_height(img);
  int neww = (int)(((float)(settings.height) * ((float)(w) / (float)(h))) + 0.49999999);
  img = scale_surface (img, w, h, neww, settings.height);
  h = settings.height;
  w = neww;
  /* Attempt to center the image if it is not the height of the line. */
  int image_offset = (h - settings.height) / 2;
  cairo_set_source_surface(cr, img, offset.x, offset.image_y - h + image_offset);
  cairo_mask_surface(cr, img, offset.x, offset.image_y - h + image_offset);

  return w;
}

/* @brief Get character between % (function called from parse_response_line).
 * @param c A reference to the pointer to the current section
 * @param data A pointer to the data variable
 */
void get_in(char **c, char **data) {
    *c += 2;
    *data = *c;
    while (**c != '%') {
        *c += 1;
    }
}


/* @brief Parses the text pointed to by *c and moves *c to
 *        a new location.
 *
 * @param *cr a cairo context (used to know the space used by the font).
 * @param[in/out] c A reference to the pointer to the current section
 * @param line_length length in pixel of the line.
 * @return A populated draw_t type.
 */
static draw_t parse_response_line(cairo_t *cr, char **c, uint32_t line_length) {
  if (!c || !*c) {
    fprintf(stderr, "Invalid parse state");
    return (draw_t){ DRAW_TEXT, NULL }; /* This will invoke a segfault most likely. */
  }

  char *data = NULL;
  draw_type_t type = DRAW_TEXT;

  /* We've found a sequence of some kind. */
  if (**c == '%') {
    switch (*(*c+1)) {
      case 'I':
        type = DRAW_IMAGE;
        get_in(c, &data);
        break;
      case 'B':
        type = BOLD;
        get_in(c, &data);
        break;
      case 'N':
        type = NEW_LINE;
        *c += 2;
        break;
      default:
        *c += 1;
        data = *c;
        break;
    }

  } else {
    /* Escape character. */
    if (**c == '\\' && *(*c + 1) == '%') {
      /* Skip the \ in the output. */
      data = *c + 1;
      /* Skip the check for %. */
      *c += 2;
    } else {
      data = *c;
    }
    type = DRAW_TEXT;

    cairo_text_extents_t extents;
    /* http://cairographics.org/manual/cairo-cairo-scaled-font-t.html#cairo-text-extents-t
     * For more information on cairo text extents.
     */
    cairo_text_extents(cr, data, &extents);
    double base_line_length = extents.x_advance;
    /* length of the line from the data variable position */
    if (base_line_length > line_length) {
        /* Checking if the text is long enough to exceed the line length
         * so we know if we have to check when the line is full.
         */
        while (**c != '\0' && **c != '%'
               && !(**c == '\\' && *(*c + 1) == '%')) {
            *c += 1;
            cairo_text_extents(cr, *c, &extents);
            if ((base_line_length - extents.x_advance) > line_length) {
                *c -= 1;
                if (*c == data)
                    data = NULL;
                break;
            }
        }
    } else {
        while (**c != '\0' && **c != '%'
               && !(**c == '\\' && *(*c + 1) == '%')) {
            *c += 1;
        }
    }
  }

  return (draw_t){ type, data };
}

/* @brief Draw a line of text to a cairo context.
 *
 * @param cr A cairo context for drawing to the screen.
 * @param text The text to be drawn.
 * @param line The index of the line to be drawn (counting from the top).
 * @param foreground The color of the text.
 * @param background The color of the background.
 * @return Void.
 */
static void draw_line(cairo_t *cr, const char *text, uint32_t line, color_t *foreground, color_t *background) {
  pthread_mutex_lock(&global.draw_mutex);

  cairo_set_source_rgb(cr, background->r, background->g, background->b);
  cairo_rectangle(cr, 0, line * settings.height, settings.width, (line + 1) * settings.height);
  cairo_stroke_preserve(cr);
  cairo_fill(cr);
  offset_t offset = calculate_line_offset(line);

  /* Parse the response line as we draw it. */
  char *c = (char *)text;
  while (c && *c != '\0') {
    draw_t d = parse_response_line(cr, &c, settings.width - offset.x);
    if (d.data == NULL)
        break;
    /* Checking if there are still char to draw. */

    char saved = *c;
    *c = '\0';
    switch (d.type) {
      case DRAW_IMAGE:
        offset.x += draw_image(cr, d.data, offset) + settings.height / 10;
        break;
      case BOLD:
        offset.x += draw_text(cr, d.data, offset, foreground, CAIRO_FONT_WEIGHT_BOLD);
        break;
      case NEW_LINE:
        break;
      case DRAW_TEXT:
      default:
        offset.x += draw_text(cr, d.data, offset, foreground, CAIRO_FONT_WEIGHT_NORMAL);
        break;
    }
    *c = saved;
  }

  pthread_mutex_unlock(&global.draw_mutex);
}

/* @brief Draw a description to a cairo context.
 *
 * @param cr A cairo context for drawing to the screen.
 * @param text The description to be drawn.
 * @param foreground The color of the text.
 * @param background The color of the background.
 * @return Void.
 */
static void draw_desc(cairo_t *cr, const char *text, color_t *foreground, color_t *background) {
  pthread_mutex_lock(&global.draw_mutex);
  cairo_set_source_rgb(cr, background->r, background->g, background->b);
  cairo_rectangle(cr, settings.width, 0,
          settings.width+settings.desc_size, settings.height*(global.result_count+1));
  cairo_stroke_preserve(cr);
  cairo_fill(cr);
  offset_t offset = {settings.width, global.real_font_size, global.real_font_size};

  /* Parse the response line as we draw it. */
  char *c = (char *)text;
  while (c && *c != '\0') {
    draw_t d = parse_response_line(cr, &c, settings.desc_size + settings.width - offset.x);
    char saved = *c;
    *c = '\0';
    switch (d.type) {
      case DRAW_IMAGE:
        offset.x += draw_image(cr, d.data, offset) + settings.height / 10;
        break;
      case NEW_LINE:
        offset.x = settings.width;
        offset.y += settings.font_size;
        break;
      case BOLD:
        offset.x += draw_text(cr, d.data, offset, foreground, 1);
        break;
      case DRAW_TEXT:
      default:
        offset.x += draw_text(cr, d.data, offset, foreground, 0);
        break;
    }
    *c = saved;
    if ((offset.x + settings.font_size) > (settings.width + settings.desc_size)) {
        /* Checking if it's gonna write out of the square space. */
        offset.x = settings.width;
        offset.y += global.real_font_size;
    }
  }

  pthread_mutex_unlock(&global.draw_mutex);
}



/* @brief Draw the query text (what is typed).
 *
 * @param cr A cairo context for drawing to the screen.
 * @param surface A cairo surface for drawing to the screen.
 * @param text The string to draw into the query field (what is being typed).
 * @param cursor The current index of the cursor
 * @return Void.
 */
static void draw_query_text(cairo_t *cr, cairo_surface_t *surface, const char *text, uint32_t cursor) {
  draw_typed_line(cr, (char *)text, 0, cursor, &settings.query_fg, &settings.query_bg);
  cairo_surface_flush(surface);
}

/* @brief Draw the responses to the query.
 *
 * Note: the window may be resized in this function.
 *
 * @param connection A connection to the Xorg server.
 * @param window An xcb window created by xcb_generate_id.
 * @param cr A cairo context for drawing to the screen.
 * @param surface A cairo surface for drawing to the screen.
 * @param results An array of results to be drawn.
 * @param result_count The number of results to be drawn.
 * @return Void.
 */
static void draw_response_text(xcb_connection_t *connection, xcb_window_t window, cairo_t *cr, cairo_surface_t *surface, result_t *results) {
  int32_t line, index;
  if (global.result_count - 1 < global.result_highlight) {
    global.result_highlight = global.result_count - 1;
  }

  uint32_t max_results = settings.max_height / settings.height - 1;
  uint32_t display_results = min(global.result_count, max_results);
  if ((global.result_offset + display_results) < (global.result_highlight + 1)) {
    global.result_offset = global.result_highlight - (display_results - 1);
    display_results = global.result_count - global.result_offset;
  } else if (global.result_offset > global.result_highlight) {
    global.result_offset = global.result_highlight;
  }

  if ((global.result_highlight < global.result_count) &&
          results[global.result_highlight].desc) {
      if (settings.auto_center) {
        uint32_t values[] = { global.win_x_pos_with_desc, global.win_y_pos };
        xcb_configure_window(connection, window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
      }

      uint32_t new_height = min(settings.height * (global.result_count + 1), settings.max_height);
      uint32_t values[] = { settings.width+settings.desc_size, new_height };
      xcb_configure_window(connection, window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
      cairo_xcb_surface_set_size(surface, settings.width+settings.desc_size, new_height);
      draw_desc(cr, results[global.result_highlight].desc, &settings.highlight_fg, &settings.highlight_bg);
  } else {
      if (settings.auto_center) {
        uint32_t values[] = { global.win_x_pos, global.win_y_pos };
        xcb_configure_window(connection, window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
      }

      uint32_t new_height = min(settings.height * (global.result_count + 1), settings.max_height);
      uint32_t values[] = { settings.width, new_height };
      xcb_configure_window (connection, window, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
      cairo_xcb_surface_set_size(surface, settings.width, new_height);
  }

  for (index = global.result_offset, line = 1; index < global.result_offset + display_results; index++, line++) {
    if (index != global.result_highlight) {
      draw_line(cr, results[index].text, line, &settings.result_fg, &settings.result_bg);
    } else {
      draw_line(cr, results[index].text, line, &settings.highlight_fg, &settings.highlight_bg);
    }
  }
  cairo_surface_flush(surface);
  xcb_flush(connection);
}

/* @brief Calls the associated redraw functions of both query and response text.
 *
 * @param connection A connection to the Xorg server.
 * @param window An xcb window created by xcb_generate_id.
 * @param cr A cairo context for drawing to the screen.
 * @param surface A cairo surface for drawing to the screen.
 * @param query_string The string to draw into the query field (what is being typed).
 * @param query_cursor_index The current index of the cursor
 * @return Void.
 */
static void redraw_all(xcb_connection_t *connection, xcb_window_t window, cairo_t *cr, cairo_surface_t *surface, char *query_string, uint32_t query_cursor_index) {
  draw_query_text(cr, surface, query_string, query_cursor_index);
  draw_response_text(connection, window, cr, surface, global.results);
}

/* @brief Parses text to populate a results structure.
 *
 * note: An allocation is done in this function, so results should be freed.
 *
 * @param text The text to be parsed.
 * @param length The length of the text passed in (in bytes).
 * @param results A reference to the results to be populated.
 * @return Number of results parsed.
 */
static uint32_t parse_response_text(char *text, size_t length, result_t **results) {
  int32_t index, mode;
  mode = 0; /* 0 -> closed, 1 -> opened no command (action), 2 -> opened, command (desc)*/
  result_t *ret = calloc(1, sizeof(result_t));
  uint32_t count = 0;
  for (index = 0; text[index] != 0 && index < length; index++) {
    /* Escape sequence. */
    if (text[index] == '\\' && index + 1 < length) {
      switch (text[index+1]) {
        case '{':
        case '|':
        case '}':
          memmove(&text[index], &(text[index+1]), length - index);
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
      ret = realloc(ret, count * sizeof(ret[0]));
      if (index + 1 < length) {
        ret[count - 1].text = &(text[index+1]);
      }
      mode++;
    }
    /* Split brace. */
    else if (text[index] == '|') {
      if ((mode != 1) && (mode != 2)) {
        fprintf(stderr, "Syntax error, found | at index %d.\n %s\n", index, text);
        free(ret);
        return 0;
      }
      text[index] = 0;
      if ((index + 1 < length) && (mode == 1)){
        ret[count - 1].action = &(text[index+1]);
        /* Can be a description or an action */
      }
      if ((index + 1 < length) && (mode == 2)){
        ret[count - 1].desc = &(text[index+1]);
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
      if (mode == 2) {
        /* if no description was used in the user script. */
        ret[count - 1].desc = NULL;
      }
      text[index] = 0;
      mode = 0;
    }
  }
  *results = ret;
  return count;
}

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

/* @brief Reads from the child process's standard out in a loop.  Meant to be used
 *        as a spawned thread.
 *
 * @param args Immediately cast to a result_params_t type struct.  See that struct
 *        for more information.
 * @return NULL.
 */
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
    uint32_t result_count = parse_response_text(global.result_buf, res, &results);
    pthread_mutex_lock(&global.result_mutex);
    if (global.results && results != global.results) {
      free(global.results);
    }
    global.results = results;
    global.result_count = result_count;
    debug("Recieved %d results.\n", result_count);
    if (global.result_count) {
        draw_response_text(connection, window, cairo_context, cairo_surface, results);
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
static int32_t write_to_remote(FILE *child, char *format, ...) {
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
static inline int32_t process_key_stroke(xcb_window_t window, char *query_buffer, uint32_t *query_index, uint32_t *query_cursor_index, xcb_keysym_t key, xcb_connection_t *connection, cairo_t *cairo_context, cairo_surface_t *cairo_surface, FILE *to_write) {
  pthread_mutex_lock(&global.result_mutex);

  /* Check when we should update. */
  int32_t redraw = 0;
  int32_t resend = 0;

  debug("key: %u\n", key);

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
      if (global.result_highlight > 0) {
        global.result_highlight--;
        draw_response_text(connection, window, cairo_context, cairo_surface, global.results);
      }
      break;
    case 65364: /* Down. */
      if (global.result_count && global.result_highlight < global.result_count - 1) {
        global.result_highlight++;
        draw_response_text(connection, window, cairo_context, cairo_surface, global.results);
      }
      break;
    case 65289: /* Tab. */
      if (global.result_count && global.result_highlight < global.result_count - 1) {
        global.result_highlight++;
        draw_response_text(connection, window, cairo_context, cairo_surface, global.results);
      } else if(global.result_count && global.result_highlight == global.result_count - 1) {
        global.result_highlight = 0;
        draw_response_text(connection, window, cairo_context, cairo_surface, global.results);
      }
      break;
    case 65056: /* Shift Tab */
      if (global.result_count && global.result_highlight > 0) {
        global.result_highlight--;
        draw_response_text(connection, window, cairo_context, cairo_surface, global.results);
      } else if(global.result_count && global.result_highlight == 0) {
        global.result_highlight = global.result_count - 1;
        draw_response_text(connection, window, cairo_context, cairo_surface, global.results);
      }
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

/* @brief Spawns a process (via fork) and sets up pipes to allow communication with
 *        the user defined executable.
 *
 * @param file The user defined file to load into the newly spawned process.
 * @param to_child_fd The fd used to write to the child process.
 * @param from_child_fd The fd used to read from the child process.
 * @return 0 on success and 1 on failure.
 */
static int32_t spawn_piped_process(char *file, int32_t *to_child_fd, int32_t *from_child_fd, char **argv) {
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
    fprintf(stderr, "Couldn't spawn cmd: %s\n", strerror(errno));
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
    fprintf(stderr, "Couldn't execute file: %s\n", strerror(errno));
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
        config_file = optarg;
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
        xcb_keysym_t key = xcb_key_press_lookup_keysym(keysyms, k, k->state & ~XCB_MOD_MASK_2);
        int32_t ret = process_key_stroke(window, query_string, &query_index, &query_cursor_index, key, connection, cairo_context, cairo_surface, to_child);
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
