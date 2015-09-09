/** @file display.c
 *  @author Bram Wasti <bwasti@cmu.edu>
 *
 *  @brief This file contains the logic that draws to the screen.
 */

#include <stdio.h>
#include <unistd.h>
#include <wordexp.h>

#include "display.h"
#include "globals.h"

#define min(a,b) ((a) < (b) ? (a) : (b))

/* @brief Type used to pass around x,y offsets. */
typedef struct {
  uint32_t x;
  uint32_t y;
  uint32_t image_y;
} offset_t;

/* @brief Returns the offset for a line of text.
 *
 * @param line the index of the line to be drawn (counting from the top).
 * @return the line's offset.
 */
static inline offset_t calculate_line_offset(uint32_t line) {
  offset_t result;
  result.x  = settings.horiz_padding;
  result.y  = settings.height * line;
  result.image_y = result.y;
  result.y +=  + global.real_font_size;
  /* To draw a picture cairo need to know the top left corner
   * position. But to draw a text cairo need to know the bottom
   * left corner position.
   */

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
static uint32_t draw_text(cairo_t *cr, const char *text, offset_t offset, color_t *foreground, cairo_font_weight_t weight, uint32_t font_size) {
  cairo_text_extents_t extents;
  cairo_text_extents(cr, text, &extents);
  cairo_move_to(cr, offset.x, offset.y);
  cairo_set_source_rgb(cr, foreground->r, foreground->g, foreground->b);
  cairo_select_font_face(cr, settings.font_name, CAIRO_FONT_SLANT_NORMAL, weight);
  cairo_set_font_size(cr, font_size);
  cairo_show_text(cr, text);
  return extents.x_advance;
}

/* @brief Return the new format for a picture to fit in a window.
 *
 * @param Current offset in the line/desc, used to know the image position.
 * @param width Width of the picture.
 * @param height Height of the picture.
 * @param win_size_x Width of the window.
 * @param win_size_y Height of the window.
 *
 * @return The advance in the x and y direction.
 */
static image_format_t get_new_size(uint32_t width, uint32_t height, uint32_t win_size_x, uint32_t win_size_y) {
  image_format_t new_format = { width, height };
  if (width > win_size_x || height > win_size_y) {
      /* Formatting only the big picture. */
      float prop = min((float)win_size_x / width,
              (float)win_size_y / height);
      /* Finding the best proportion to fit the picture. */
      new_format.width = prop * width;
      new_format.height = prop * height;

      debug("Resizing the image to %ix%i (prop = %f)\n", new_format.width, new_format.height, prop);
  }
  return new_format;
}

/* @brief Draw an image at the given offset.
 *
 * @param cr A cairo context for drawing to the screen.
 * @param file The image to be drawn.
 * @param Current offset in the line/desc, used to know the image position.
 * @param win_size_x Width of the window.
 * @param win_size_y Height of the window.
 * @return The advance in the x direction.
 */
static void draw_image_with_gdk(cairo_t *cr, const char *file, offset_t offset, uint32_t win_size_x, uint32_t win_size_y, image_format_t *format) {
  GdkPixbuf *image;
  GError *error = NULL;

  image = gdk_pixbuf_new_from_file(file, &error);
  if (image == NULL) {
      debug("Image opening failed (tried to open %s): %s\n", file, error->message);
      g_error_free(error);
      return ;
  }

  image_format_t new_form;
  new_form = get_new_size(gdk_pixbuf_get_width(image), gdk_pixbuf_get_height(image), win_size_x, win_size_y);
  *format = new_form;

  /* Resizing */
  GdkPixbuf *resize =  gdk_pixbuf_scale_simple(image, new_form.width, new_form.height, GDK_INTERP_BILINEAR);

  gdk_cairo_set_source_pixbuf(cr, resize, offset.x, offset.image_y);
  cairo_paint (cr);
}

/* @brief Draw an image at the given offset.
 *
 * @param cr A cairo context for drawing to the screen.
 * @param file The image to be drawn.
 * @param Current offset in the line/desc, used to know the image position.
 * @param win_size_x Width of the window.
 * @param win_size_y Height of the window.
 * @return The advance in the x direction.
 */
static image_format_t draw_image(cairo_t *cr, const char *file, offset_t offset, uint32_t win_size_x, uint32_t win_size_y) {
  wordexp_t expanded_file;
  image_format_t format = {0, 0};

  if (wordexp(file, &expanded_file, 0)) {
    fprintf(stderr, "Error expanding file %s\n", file);
  } else {
    file = expanded_file.we_wordv[0];
  }

  if (access(file, F_OK) == -1) {
    fprintf(stderr, "Cannot open image file %s\n", file);
    format.width = 0;
    format.height = 0;
    return format;
  }

  FILE *picture = fopen(file, "r");
  switch (fgetc(picture)) {
    /* https://en.wikipedia.org/wiki/Magic_number_%28programming%29#Magic_numbers_in_files */
    case 137:
        debug("PNG found\n");
        draw_image_with_gdk(cr, file, offset, win_size_x, win_size_y, &format);
        break;
    case 255:
        debug("JPEG found\n");
        draw_image_with_gdk(cr, file, offset, win_size_x, win_size_y, &format);
        break;
    case 47:
        debug("GIF found\n");
        draw_image_with_gdk(cr, file, offset, win_size_x, win_size_y, &format);
        break;
    default:
        debug("Unknown image format found: %s\n", file);
        break;
  }
  fclose(picture);
  return format;
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
  /* Add 2 offset to height to prevent flickery drawing over the typed text.
   * TODO: Use better math all around. */
  cairo_rectangle(cr, 0, line * settings.height + 2, settings.width, (line + 1) * settings.height);
  cairo_stroke_preserve(cr);
  cairo_fill(cr);
  offset_t offset = calculate_line_offset(line);

  /* Parse the result line as we draw it. */
  char *c = (char *)text;
  while (c && *c != '\0') {
    draw_t d = parse_result_line(cr, &c, settings.width - offset.x);
    if (d.data == NULL)
        break;
    /* Checking if there are still char to draw. */

    char saved = *c;
    *c = '\0';
    switch (d.type) {
      case DRAW_IMAGE: ;
        image_format_t format;
        format = draw_image(cr, d.data, offset, settings.width - offset.x, settings.height);
        offset.x += format.width;
        break;
      case BOLD:
        offset.x += draw_text(cr, d.data, offset, foreground, CAIRO_FONT_WEIGHT_BOLD, settings.font_size);
        break;
      case DRAW_LINE:
      case NEW_LINE:
        break;
      case CENTER:
        offset.x += (settings.desc_size - d.data_length) / 2;
      case DRAW_TEXT:
      default:
        offset.x += draw_text(cr, d.data, offset, foreground, CAIRO_FONT_WEIGHT_NORMAL, settings.font_size);
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
  uint32_t desc_height = settings.height*(global.result_count+1);
  cairo_rectangle(cr, settings.width + 2, 0,
          settings.width+settings.desc_size, desc_height);
  cairo_stroke_preserve(cr);
  cairo_fill(cr);
  offset_t offset = {settings.width + 2, global.real_desc_font_size, 0};

  /* Parse the result line as we draw it. */
  char *c = (char *)text;
  while (c && *c != '\0') {
    draw_t d = parse_result_line(cr, &c, settings.desc_size + settings.width - offset.x);
    char saved = *c;
    *c = '\0';
    switch (d.type) {
      case DRAW_IMAGE: ;
        image_format_t format;
        format = draw_image(cr, d.data, offset, settings.desc_size, desc_height - offset.image_y);
        offset.image_y += format.height;
        offset.y = offset.image_y;
        offset.x += format.width;
        /* We set the offset.y and x next to the picture so the user can choose to
         * return to the next line or not.
         */
        break;
      case DRAW_LINE:
        offset.y += (global.real_desc_font_size / 2);
        offset.x = settings.width;
        cairo_set_source_rgb(cr, settings.result_bg.r, settings.result_bg.g, settings.result_bg.b);
        cairo_move_to(cr, offset.x + settings.line_gap, offset.y);
        cairo_line_to(cr, offset.x + settings.desc_size - settings.line_gap, offset.y);
        cairo_stroke(cr);
        offset.y += global.real_desc_font_size;
        offset.image_y += 2 * global.real_desc_font_size;
        break;
      case NEW_LINE:
        offset.x = settings.width;
        offset.y += global.real_desc_font_size;
        offset.image_y += global.real_desc_font_size;
        break;
      case BOLD:
        offset.x += draw_text(cr, d.data, offset, foreground, CAIRO_FONT_WEIGHT_BOLD, settings.desc_font_size);
        break;
      case CENTER:
        if (d.data_length < settings.desc_size)
            offset.x += (settings.desc_size - d.data_length) / 2;
      case DRAW_TEXT:
      default:
        offset.x += draw_text(cr, d.data, offset, foreground, CAIRO_FONT_WEIGHT_NORMAL, settings.desc_font_size);
        break;
    }
    *c = saved;
    if ((offset.x + settings.desc_font_size) > (settings.width + settings.desc_size)) {
        /* Checking if it's gonna write out of the square space. */
        offset.x = settings.width;
        offset.y += global.real_desc_font_size;
        offset.image_y += global.real_desc_font_size;
    }
  }

  pthread_mutex_unlock(&global.draw_mutex);
}

void draw_query_text(cairo_t *cr, cairo_surface_t *surface, const char *text, uint32_t cursor) {
  draw_typed_line(cr, (char *)text, 0, cursor, &settings.query_fg, &settings.query_bg);
  cairo_surface_flush(surface);
}

void draw_result_text(xcb_connection_t *connection, xcb_window_t window, cairo_t *cr, cairo_surface_t *surface, result_t *results) {
  int32_t line, index;
  if (global.result_count - 1 < global.result_highlight) {
    global.result_highlight = global.result_count - 1;
  }

  uint32_t max_results = settings.max_height / settings.height - 1;
  uint32_t display_results = min(global.result_count, max_results);
  /* Set the offset. */
  if (global.result_count <= max_results) {
      /* Sometime you use an offset from a previous query and then you send another query
       * to your script but get only 2 response, you need to reset the offset to 0
       */
      global.result_offset = 0;
  } else if (global.result_count - max_results < global.result_offset) {
      /* When we need to adjust the offset. */
      global.result_offset = global.result_count - max_results;
  } else if ((global.result_offset + display_results) < (global.result_highlight + 1)) {
      /* Change the offset to match the highlight when scrolling down. */
      global.result_offset = global.result_highlight - (display_results - 1);
      display_results = global.result_count - global.result_offset;
  } else if (global.result_offset > global.result_highlight) {
      /* Used when scrolling up. */
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
      cairo_xcb_surface_set_size(surface, settings.width + settings.desc_size, new_height);
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
    if (!(results[index].action)) {
      /* Title */
      draw_line(cr, results[index].text, line, &settings.result_fg, &settings.result_bg);
    } else if (index != global.result_highlight) {
      draw_line(cr, results[index].text, line, &settings.result_fg, &settings.result_bg);
    } else {
      draw_line(cr, results[index].text, line, &settings.highlight_fg, &settings.highlight_bg);
    }
  }
  cairo_surface_flush(surface);
  xcb_flush(connection);
}

void redraw_all(xcb_connection_t *connection, xcb_window_t window, cairo_t *cr, cairo_surface_t *surface, char *query_string, uint32_t query_cursor_index) {
  draw_query_text(cr, surface, query_string, query_cursor_index);
  draw_result_text(connection, window, cr, surface, global.results);
}

