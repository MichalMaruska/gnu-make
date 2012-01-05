/* Interface for all user interface output.
Copyright (C) 2011 Free Software Foundation, Inc.
This file is part of GNU Make.

GNU Make is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 3 of the License, or (at your option) any later
version.

GNU Make is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "output.h"
#include "make.h"
#include "debug.h"
#include <stdio.h>
#include <assert.h>


#define COLOR_BOLD_RED      "1;31"
#define COLOR_CYAN          "0;36"
#define COLOR_GREEN         "0;32"
#define COLOR_BOLD_MAGENTA  "1;35"

#define ERASE_IN_LINE   "\033[K"


/* TODO resolve use of "extern" */

/* Value of the MAKELEVEL variable at startup (or 0).  */
extern unsigned int makelevel;

/* The name we were invoked with.  */
extern char *program;

/* From misc.c. */
extern char * xstrndup (const char *str, unsigned int length);


/* Nonzero means do colorize output.  */
int color_flag;

/* Nonzero means do output "\033[K" after color open and close.  */
int erase_in_line_flag = 1;

/* These colors are uses for output.  Can be overridden from "MAKE_COLORS". */
const char * color_dir_enter = COLOR_CYAN;
const char * color_dir_leave = COLOR_CYAN;
const char * color_misc_message = COLOR_GREEN;
const char * color_misc_error = COLOR_BOLD_RED;
const char * color_misc_fatal = COLOR_BOLD_RED;
const char * color_execution = COLOR_BOLD_MAGENTA;


#define PREVENT_NULL(s)  ((s) ? (s) : "<error>")


typedef struct _char_range_t {
	const char * first;
	const char * after_last;
} char_range_t;

#define RANGE_LEN(range)  (range.after_last - range.first)
#define RANGE_DUP(range)  xstrndup(range.first, RANGE_LEN(range))
#define RANGE_EQUALS(range, text)  \
    ( ! strncmp(text, range.first, RANGE_LEN(range)) \
        && ((long)strlen(text) == RANGE_LEN(range)))
#define RANGE_SET(range, _first, _after_last)  \
    do \
      { \
        range.first = _first; \
        range.after_last = _after_last; \
      } \
    while (0)
#define EMPTY_RANGE  { 0, 0 }


typedef struct _mapping_def_t {
	const char * key;
	int * flag_destination;
	const char ** color_destination;
} mapping_def_t;

/* Find the index of a certain name in an array of mapping_def_t instances */
#define FIND(haystack_array, member, needle_range, match_index) \
    do \
      { \
        size_t u = 0; \
        match_index = -1; \
        for (; u < sizeof(haystack_array) / sizeof(haystack_array[0]); u++) \
          { \
            const char * const key = haystack_array[u].member; \
            if (RANGE_EQUALS(needle_range, key)) \
              { \
                match_index = u; \
                break; \
              } \
          } \
      } \
    while(0)


void apply_make_colors()
{
  const mapping_def_t valid_names[] = {
    {"enter", 0, &color_dir_enter},
    {"leave", 0, &color_dir_leave},
    {"message", 0, &color_misc_message},
    {"error", 0, &color_misc_error},
    {"fatal", 0, &color_misc_fatal},
    {"run", 0, &color_execution},
    {"erase", &erase_in_line_flag, 0},
  };

  int name_index = -1;
  const char * const MAKE_COLORS = getenv("MAKE_COLORS");
  const char * key_pos = MAKE_COLORS;
  const char * colon_pos = NULL;
  char_range_t name = EMPTY_RANGE;
  char_range_t value = EMPTY_RANGE;
  if (! MAKE_COLORS)
    return;

  /* Example: MAKE_COLORS='erase=no:enter=0;42:leave=0;41:message=0' */
  for (; key_pos != 0; key_pos = colon_pos + 1)
    {
      const char * const assign_pos = strchr(key_pos, '=');
      if (! assign_pos) {
        fatal (0, "Assignment ('=') missing in MAKE_COLORS: \"%s\"", key_pos);
      }

      colon_pos = strchr(assign_pos, ':');
      if (! colon_pos)
        colon_pos = assign_pos + strlen(assign_pos);

      RANGE_SET(name, key_pos, assign_pos);
      RANGE_SET(value, assign_pos + 1, colon_pos);

      if (RANGE_LEN(name) > 0)
        {
          FIND(valid_names, key, name, name_index);
          if (name_index == -1)
            {
              char * const s = RANGE_DUP(name);
              fatal (0, "Invalid name in MAKE_COLORS: \"%s\"", PREVENT_NULL(s));
              free(s);
            }
         }
      else
        fatal (0, "Empty name in MAKE_COLORS: \"%s\"", key_pos);

      /* Which kind of statement do we have? */
      if (valid_names[name_index].flag_destination)
        {
          /* Boolean statement */
          int * const destination = valid_names[name_index].flag_destination;
          if (RANGE_LEN(value) <= 0)
            {
              const char * const switch_name = valid_names[name_index].key;
              fatal (0, "Empty value for switch \"%s\" in MAKE_COLORS", switch_name);
            }
          else if (RANGE_EQUALS(value, "yes"))
            {
              DB(DB_VERBOSE, ("Erase in line enabled\n"));
              *destination = 1;
            }
          else if (RANGE_EQUALS(value, "no"))
            {
              DB(DB_VERBOSE, ("Erase in line disabled\n"));
              *destination = 0;
            }
          else
            {
              /* Invalid (i.e. neither "yes" nor "no") */
              const char * const switch_name = valid_names[name_index].key;
              char * const guilty_part = RANGE_DUP(value);
              fatal (0, "Invalid value for switch \"%s\" in MAKE_COLORS: \"%s\"", switch_name, PREVENT_NULL(guilty_part));
              free(guilty_part);
            }
        }
      else
        {
          /* Colorization statement */
          const int value_is_valid = RANGE_LEN(value) > 0;
          if (value_is_valid)
            {
              const char * const class_name = valid_names[name_index].key;
              const char * const color = PREVENT_NULL(RANGE_DUP(value));
              const char ** const color_destination = valid_names[name_index].color_destination;
              DB(DB_VERBOSE, ("Applying color \"%s\" to class \"%s\"\n", color, class_name));
              *color_destination = color;
            }
          else
            {
              const char_range_t guilty_range = { key_pos, colon_pos };
              char * const guilty_part = RANGE_DUP(guilty_range);
              fatal (0, "Invalid color mapping in MAKE_COLORS: \"%s\"", PREVENT_NULL(guilty_part));
              free(guilty_part);
            }
        }

      /* Done? */
      if (colon_pos[0] == '\0')
        break;
  }
}


void voutputf(message_t type, const struct floc *flocp, int flags, const char * format, va_list args)
{
  FILE * target = NULL;
  const char * color = NULL;
  int colorize = color_flag;
  const int append_newline = (flags & OF_APPEND_NEWLINE);

  if ((format == 0) || (format[0] == '\0'))
    return;

  /* Determine target file (i.e. stdout or stderr) and color to pick */
  switch (type)
    {
      case OT_DIR_ENTER: target = stdout; color = color_dir_enter; break;
      case OT_DIR_LEAVE: target = stdout; color = color_dir_leave; break;
      case OT_MISC_MESSAGE: target = stdout; color = color_misc_message; break;
      case OT_MISC_ERROR: target = stderr; color = color_misc_error; break;
      case OT_MISC_FATAL: target = stderr; color = color_misc_fatal; break;
      case OT_EXECUTION: target = stdout; color = color_execution; break;
      default: target = stdout; color = ""; colorize = 0; break;
    }
  assert(target);
  assert(color);

  /* Output color start */
  if (colorize)
    fprintf (target, "\033[%sm%s", color,
        erase_in_line_flag ? ERASE_IN_LINE : "");

  /* Output  prefix */
  if (flags & OF_PREPEND_PREFIX)
    {
      const char * catchy = (type == OT_MISC_FATAL) ? "*** " : "";
      if (flocp && flocp->filenm)
        fprintf (target, "%s:%lu: %s", flocp->filenm, flocp->lineno, catchy);
      else if (makelevel == 0)
        fprintf (target, "%s: %s", program, catchy);
      else
        fprintf (target, "%s[%u]: %s", program, makelevel, catchy);
    }

  /* Output actual message */
  /* TODO make more portabe, see misc.c */
  vfprintf (target, format, args);

  if (type == OT_MISC_FATAL)
    fputs (_(".  Stop."), target);

  /* Output finishing newline and color stop */
  if (colorize)
    fprintf (target, "%s\033[m%s",
        append_newline ? "\n" : "",
		erase_in_line_flag ? ERASE_IN_LINE : "");
  else if (append_newline)
    fputc ('\n', target);

  /* Flush */
  fflush(target);
}


void outputf(message_t type, const struct floc *flocp, int flags, const char * format, ...)
{
  va_list args;

  /* TODO make more portabe, see misc.c */
  va_start (args, format);
  voutputf(type, flocp, flags, format, args);
  va_end (args);
}
