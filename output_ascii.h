/* Interface for all user interface output.
Copyright (C) 2012 Free Software Foundation, Inc.
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

#ifndef _output_ascii_h_
#define _output_ascii_h_


/* Nonzero means do colorize output printed.  */
extern int color_flag;


#include <stdarg.h>  /* TODO make more portabe, see misc.c */


struct floc;

void outputf_ascii(int, const struct floc *, const char *, ...);
void voutputf_ascii(int, const struct floc *, const char *, va_list);

void apply_make_colors();


#endif /* not _output_h_ */
