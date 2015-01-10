#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

#include <unistd.h>
#include <sys/types.h>

#include <xcb/xcb.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>

#include <xcb_keysyms.h>

#define HEIGHT    50
#define WIDTH     500

/* Globals. */

static inline int check_xcb_cookie(xcb_void_cookie_t cookie, xcb_connection_t *connection, char *error) {
  xcb_generic_error_t *xcb_error = xcb_request_check(connection, cookie);
  if (xcb_error) {
    fprintf(stderr, "[error:%"PRIu8"] %s\n", xcb_error->error_code, error);
    return 1;
  }

  return 0;
}

/*static xcb_gcontext_t get_font_gc(xcb_connection_t *connection, xcb_screen_t *screen, xcb_window_t window, const char *font_name) {

  xcb_font_t font = xcb_generate_id(connection);
  uint16_t name_len = strlen(font_name);
  xcb_void_cookie_t font_cookie = xcb_open_font_checked(connection, font, name_len, font_name);
  check_xcb_cookie(font_cookie, connection, "Unable to open font.");

  xcb_gcontext_t gc = xcb_generate_id(connection);
  uint32_t mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT;
  uint32_t values[3] = { screen->black_pixel, screen->white_pixel, font };
  xcb_void_cookie_t gc_cookie = xcb_create_gc(connection, gc, screen->root, mask, values);
  check_xcb_cookie(gc_cookie, connection, "Couldn't create a graphical context.");

  font_cookie = xcb_close_font_checked(connection, font);
  check_xcb_cookie(font_cookie, connection, "Unable to close the font.");

  return gc;
}

static void draw_text(xcb_connection_t *connection, xcb_screen_t *screen,
                      xcb_window_t window, int16_t x, int16_t y, const char *text, const char *font_name) {
  xcb_gcontext_t gc = get_font_gc(connection, screen, window, font_name);

  uint16_t text_len = strlen(text);
  xcb_void_cookie_t text_cookie = xcb_image_text_8_checked(connection, text_len, window, gc, x, y, text);
  check_xcb_cookie(text_cookie, connection, "Unable to draw text.");
  
  xcb_void_cookie_t gc_cookie = xcb_free_gc(connection, gc);
  check_xcb_cookie(gc_cookie, connection, "Unable to free graphic context.");
}*/

static void draw_cairo_text(cairo_t *cr, int x, int y, const char *text) {
  cairo_set_source_rgb(cr, 1, 1, 1);
  cairo_paint(cr);

  cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
  cairo_select_font_face(cr, "Source Code Pro", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 14);

  cairo_move_to(cr, x, y);
  cairo_show_text(cr, text);

  return;
}

int main(void) {

  int exit_code = 0;

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
    exit_code = -1;
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

  cairo_font_options_t *cairo_font_options = cairo_font_options_create();
  cairo_font_options_set_antialias(cairo_font_options, CAIRO_ANTIALIAS_SUBPIXEL);

  xcb_map_window(connection, window);
  xcb_flush(connection);

  /* Query string. */
  char query_string[1024];
  memset(query_string, 0, sizeof(query_string));
  unsigned int query_index = 0;

  xcb_generic_event_t *event;
  while ((event = xcb_wait_for_event(connection))) {
    switch (event->response_type & ~0x80) {
      case XCB_EXPOSE:
        draw_cairo_text(cairo_context, 10, HEIGHT-10, query_string);
        xcb_flush(connection);
        break;
      case XCB_KEY_PRESS: {
        break;
      }
      case XCB_KEY_RELEASE: {
        xcb_key_release_event_t *k = (xcb_key_release_event_t *)event;
        xcb_keysym_t key = xcb_key_press_lookup_keysym(keysyms, k, k->state);
        if (isprint(key) && query_index < 1024) {
          query_string[query_index++] = key;
        }
        draw_cairo_text(cairo_context, 10, HEIGHT-10, query_string);
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

