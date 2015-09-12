#ifndef _RESULTS_H
#define _RESULTS_H

#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>
#include <pango/pangocairo.h>

/* @brief The type of data to draw. */
typedef enum {
  DRAW_TEXT,
  DRAW_IMAGE,
  CENTER,
  BOLD,
  DRAW_LINE,
  NEW_LINE
} draw_type_t;

/* @brief Type used to pass around drawing options. */
typedef struct {
  draw_type_t type;
  char *data;
  uint32_t data_length; /* Not always filled */
} draw_t;

/* @brief Type used to maintain a list of results in a usable form. */
typedef struct {
  char *text;
  char *action;
  char *desc;
} result_t;

/* @brief This struct is exclusively used to spawn a thread. */
struct result_params {
  cairo_t *cr;
  cairo_surface_t *cr_surface;
  xcb_connection_t *connection;
  xcb_window_t window;
  int32_t fd;
};

draw_t parse_result_line(cairo_t *cr, char **c, uint32_t line_length, PangoFontDescription *font_description);
uint32_t parse_result_text(char *text, size_t length, result_t **results);
void *get_results(void *args);

#endif /* _RESULTS_H */
