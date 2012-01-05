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
#include "output_ascii.h"


void voutputf(int flags, const struct floc *flocp, const char * format, va_list args)
{
  voutputf_ascii(flags, flocp, format, args);
}


void outputf(int flags, const struct floc *flocp, const char * format, ...)
{
  va_list args;

  /* TODO make more portabe, see misc.c */
  va_start (args, format);
  voutputf(flags, flocp, format, args);
  va_end (args);
}
