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

#ifndef _output_h_
#define _output_h_


#include <stdarg.h>  /* TODO make more portabe, see misc.c */


typedef enum _output_config_t {
	OT_MASK = 0xff,
	OT_DIR_ENTER = 0x01,
	OT_DIR_LEAVE = 0x02,
	OT_MISC_MESSAGE = 0x03,
	OT_MISC_ERROR = 0x04,
	OT_MISC_FATAL = 0x05,
	OT_EXECUTION = 0x06,

	OF_PREPEND_PREFIX = 0x100
} output_config_t;


struct floc;

void outputf(int, const struct floc *, const char *, ...);
void voutputf(int, const struct floc *, const char *, va_list);


#endif /* not _output_h_ */
