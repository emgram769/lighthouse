#ifndef _GLOBALS_H
#define _GLOBALS_H

#include <pthread.h>
#include <stdint.h>

#include "results.h"

/* @brief Size of the buffers. */
#define MAX_CONFIG_SIZE   10 * 1024
#define MAX_RESULT_SIZE   10 * 1024

/* @brief Debugging utilities. */
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

/* @brief structure used to return the format of a picture.
 * It's used because you sometime only need the width of a picture(for
 * exemple if you draw a picture in a line, no need to go down), and sometime
 * you need to know both (if you draw a picture in a description).
 * */
typedef struct {
  uint32_t width;
  uint32_t height;
} image_format_t;

/* @brief A struct of globals that are used throughout the program. */
struct global_s {
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
  double real_desc_font_size;
};

/* @brief A struct of settings that are set and used when the program starts. */
struct settings_s {
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
  uint32_t desc_size; /* Size in pixel of the description window*/
  uint32_t auto_center; /* Auto center the window when the description
                         * is not expanded. */
  uint32_t desc_font_size;

  uint32_t line_gap; /* Gap between the line drawed by %L */
};

struct global_s global;
struct settings_s settings;

#endif /* _GLOBALS_H */
