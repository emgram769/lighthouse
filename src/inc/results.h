#ifndef _RESULTS_H
#define _RESULTS_H

#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>
#ifndef NO_PANGO
#include <pango/pangocairo.h>
#endif

/* @brief The type of data to draw. */
typedef enum {
  DRAW_TEXT,
  DRAW_IMAGE,
  DRAW_LINE,
  NEW_LINE
} draw_type_t;

typedef enum {
  NONE,
  CENTER,
  BOLD
} modifier_type_t;

/* @brief Type used to pass around drawing options. */
typedef struct draw_t {
  draw_type_t type;
  modifier_type_t *modifiers_array;
  uint32_t modifiers_array_length;
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

#ifndef NO_PANGO
draw_t parse_result_line(cairo_t *cr, char **c, uint32_t line_length, modifier_type_t **modifiers_array, PangoFontDescription *font_description);
#else
draw_t parse_result_line(cairo_t *cr, char **c, uint32_t line_length, modifier_type_t **modifiers_array);
#endif
uint32_t parse_result_text(char *text, size_t length, result_t **results);

#endif /* _RESULTS_H */
