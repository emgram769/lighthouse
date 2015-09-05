/** @file results.c
 *  @author Bram Wasti <bwasti@cmu.edu>
 *
 *  @brief This file contains the logic that parses results.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "globals.h"
#include "results.h"

/* @brief Get character between % (function called from parse_result_line).
 * @param c A reference to the pointer to the current section
 * @param data A pointer to the data variable
 */
static void get_in(char **c, char **data) {
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
draw_t parse_result_line(cairo_t *cr, char **c, uint32_t line_length) {
  if (!c || !*c) {
    fprintf(stderr, "Invalid parse state");
    return (draw_t){ DRAW_TEXT, NULL }; /* This will invoke a segfault most likely. */
  }

  char *data = NULL;
  draw_type_t type = DRAW_TEXT;
  uint32_t data_length = NULL;

  /* We've found a sequence of some kind. */
  if (**c == '%') {
    switch (*(*c+1)) {
      case 'I':
        type = DRAW_IMAGE;
        get_in(c, &data);
        break;
      case 'C':
        type = CENTER;
        get_in(c, &data);

        cairo_text_extents_t extents;
        /* http://cairographics.org/manual/cairo-cairo-scaled-font-t.html#cairo-text-extents-t
        * For more information on cairo text extents.
        */
        cairo_text_extents(cr, data, &extents);
        data_length = extents.x_advance;
        cairo_text_extents(cr, *c, &extents);
        data_length -= extents.x_advance;
        break;
      case 'B':
        type = BOLD;
        get_in(c, &data);
        break;
      case 'N':
        type = NEW_LINE;
        *c += 2;
        break;
      case 'L':
        type = DRAW_LINE;
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
    data_length = extents.x_advance;
    /* length of the line from the "data" variable position */
    if (data_length > line_length) {
        /* Checking if the text is long enough to exceed the line length
         * so we know if we have to check when the line is full.
         */
        while (**c != '\0' && **c != '%'
               && !(**c == '\\' && *(*c + 1) == '%')) {
            *c += 1;
            cairo_text_extents(cr, *c, &extents);
            if ((data_length - extents.x_advance) > line_length) {
                /* data_length - extents.x_advance let us know the
                 * length of the current line.
                 */
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

  return (draw_t){ type, data, data_length };
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
uint32_t parse_result_text(char *text, size_t length, result_t **results) {
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
        case '\\':
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
      if (mode == 1) {
        /* if no action */
        ret[count - 1].action = NULL;
        ret[count - 1].desc = NULL;
      }
      if (mode == 2) {
        /* if no description */
        ret[count - 1].desc = NULL;
      }
      text[index] = 0;
      mode = 0;
    }
  }
  *results = ret;
  return count;
}

