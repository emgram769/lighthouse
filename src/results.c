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
 * @param c A reference to the pointer to the current position.
 * @param data A pointer to the data variable, it will store the position of
 *              the begin of the line.
 * @param data_length A reference to the data_length variable wich will store
 *              the length of the result data.
 */
static void get_characters(cairo_t *cr, char **c, char **data, uint32_t *data_length, uint32_t line_length, PangoFontDescription *font_description) {
  /* Case we should stop the loop:
   * 1) End of the line: "\0".
   * 2) New text modification (%C, %B).
   * 3) End of text modification (%).
   * 4) End of the line.
   */
   *data = *c;

#ifndef NO_PANGO
   PangoLayout *layout;
   layout = pango_cairo_create_layout(cr);
   pango_layout_set_font_description(layout, font_description);
   pango_layout_set_text(layout, *data, -1);
   pango_cairo_update_layout(cr, layout);

   int begin_length; /* Get the line length, from the data pointer,
                      * until the '\0' character.
                      */
   pango_layout_get_pixel_size(layout, &begin_length, NULL);
   int end_length; /* Get the line length from the '*c' pointer,
                    * until the end of the line.
                    */
#else
   cairo_text_extents_t extents;
   /* http://cairographics.org/manual/cairo-cairo-scaled-font-t.html#cairo-text-extents-t
    * For more information on cairo text extents.
    */
   cairo_text_extents(cr, *data, &extents);
   int begin_length = extents.x_advance;
#endif

   while (**c != '\0' && **c != '%'
           && !(**c == '\\' && *(*c + 1) == '%')) {
       (*c)++;
#ifndef NO_PANGO
       pango_layout_set_text(layout, *c, -1);
       pango_cairo_update_layout(cr, layout);
       pango_layout_get_pixel_size(layout, &end_length, NULL);
       *data_length = (begin_length - end_length);
#else
       cairo_text_extents(cr, *c, &extents);
       *data_length = (begin_length - extents.x_advance);
#endif
       if (line_length < *data_length) {
           /* data_length - extents.x_advance let us know the
            * length of the current line.
            */
           (*c)--;
           if (**c == **data)
               *data = NULL;
           break;
       }
   }

#ifndef NO_PANGO
    g_object_unref(layout);
#endif
}

static inline void set_new_size(modifier_type_t **modifiers_array, uint32_t size) {
    modifier_type_t *tmp = realloc(*modifiers_array, size * sizeof(modifier_type_t));
    *modifiers_array = tmp;
}

/* @brief Parses the text pointed to by *c and moves *c to
 *        a new location.
 *
 * @param *cr a cairo context (used to know the space used by the font).
 * @param[in/out] c A reference to the pointer to the current section
 * @param line_length length in pixel of the line.
 * @return A populated draw_t type.
 */
#ifndef NO_PANGO
draw_t parse_result_line(cairo_t *cr, char **c, uint32_t line_length, modifier_type_t **modifiers_array, PangoFontDescription *font_description) {
#else
draw_t parse_result_line(cairo_t *cr, char **c, uint32_t line_length, modifier_type_t **modifiers_array) {
#endif
  if (!c || !*c) {
    fprintf(stderr, "Invalid parse state");
    return (draw_t){ DRAW_TEXT, NULL }; /* This will invoke a segfault most likely. */
  }

  static uint32_t modifiers_array_length = 0;

  char *data = NULL;
  draw_type_t type = DRAW_TEXT;
  uint32_t data_length = 0;

  /* We've found a sequence of some kind. */
  if (**c == '%') {
    switch (*(*c+1)) {
      case 'I':
        type = DRAW_IMAGE;
        *c += 2;
        data = *c;
        while (**c != '%') {
            *c += 1;
        }
        break;
      case 'N':
        type = NEW_LINE;
        *c += 2;
        break;
      case 'L':
        type = DRAW_LINE;
        *c += 2;
        break;
      case 'C':
        *c += 2;
        get_characters(cr, c, &data, &data_length, line_length, font_description);
        modifiers_array_length++;
        set_new_size(modifiers_array, modifiers_array_length);
        (*modifiers_array)[modifiers_array_length - 1] = CENTER;
        break;
      case 'B':
        *c += 2;
        get_characters(cr, c, &data, &data_length, line_length, font_description);
        modifiers_array_length++;
        set_new_size(modifiers_array, modifiers_array_length);
        (*modifiers_array)[modifiers_array_length - 1] = BOLD;
        break;
      case '\\':
        (*c)++; /* If '\\' is used it mean The user used a char like (C, B, I, ...)
                 * next to the %.\
                 */
      default:
        /* Return to the previous text mod state.
         * ex: %C ... %B ... % ... %
         *                   ^
         *                   |
         *                   +-- Here we need to set back the previous
         *                       type: CENTER instead of BOLD.
         */
        (*c)++; /* Passing the '%' character. */

        get_characters(cr, c, &data, &data_length, line_length, font_description);
        if (modifiers_array_length)
            modifiers_array_length--;
        set_new_size(modifiers_array, modifiers_array_length);
        break;
    }
  } else {
    /* When we are in a case like this:
     * %C ... String to long to be drawn in one line ... %
     * We just don't touch the modifiers_array so it still have the old
     * modifier used previously.
     * Those will be erased at the '%' (default case normally).
     */

    /* Escape character. */
    if (**c == '\\' && *(*c + 1) == '%') {
      /* Skip the \ in the output. */
      data = *c + 1;
      /* Skip the check for %. */
      *c += 2;
    } else {
      data = *c;
    }
    get_characters(cr, c, &data, &data_length, line_length, font_description);
  }
  return (draw_t){ type, *modifiers_array, modifiers_array_length, data, data_length };
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
      text[index] = 0;
      if (mode == 0) {
        fprintf(stderr, "Syntax error, found | at index %d.\n %s\n", index, text);
        free(ret);
        return 0;
      } else if ((index + 1 < length) && (mode == 1)){
        ret[count - 1].action = &(text[index+1]);
        /* Can be a description or an action */
      } else if ((index + 1 < length) && (mode == 2)){
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

