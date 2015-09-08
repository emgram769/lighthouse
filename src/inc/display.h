#ifndef _DISPLAY_H
#define _DISPLAY_H

#include <cairo/cairo-xcb.h>
#include <cairo/cairo.h>
#include <xcb/randr.h>
#include <xcb/xcb.h>
#include <xcb/xinerama.h>
#ifndef NO_GDK
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib.h>
#endif

#include "results.h"

/* @brief Calls the associated redraw functions of both query and result text.
 *
 * @param connection A connection to the Xorg server.
 * @param window An xcb window created by xcb_generate_id.
 * @param cr A cairo context for drawing to the screen.
 * @param surface A cairo surface for drawing to the screen.
 * @param query_string The string to draw into the query field (what is being typed).
 * @param query_cursor_index The current index of the cursor
 * @return Void.
 */
void redraw_all(xcb_connection_t *connection, xcb_window_t window, cairo_t *cr, cairo_surface_t *surface, char *query_string, uint32_t query_cursor_index);

/* @brief Draw the results to the query.
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
void draw_result_text(xcb_connection_t *connection, xcb_window_t window, cairo_t *cr, cairo_surface_t *surface, result_t *results);

/* @brief Draw the query text (what is typed).
 *
 * @param cr A cairo context for drawing to the screen.
 * @param surface A cairo surface for drawing to the screen.
 * @param text The string to draw into the query field (what is being typed).
 * @param cursor The current index of the cursor
 * @return Void.
 */
void draw_query_text(cairo_t *cr, cairo_surface_t *surface, const char *text, uint32_t cursor);

#endif /* _DISPLAY_H */
