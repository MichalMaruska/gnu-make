/* Output to stdout / stderr for GNU make
Copyright (C) 2013 Free Software Foundation, Inc.
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

#include "makeint.h"
#include "job.h"

/* GNU make no longer supports pre-ANSI89 environments.  */

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#else
# include <sys/file.h>
#endif

#ifdef WINDOWS32
# include <windows.h>
# include <io.h>
# include "sub_proc.h"
#endif /* WINDOWS32 */

struct output *output_context = NULL;
unsigned int stdio_traced = 0;

#define OUTPUT_NONE (-1)

#define OUTPUT_ISSET(_out) ((_out)->out >= 0 || (_out)->err >= 0)

#ifdef HAVE_FCNTL
# define STREAM_OK(_s) ((fcntl (fileno (_s), F_GETFD) != -1) || (errno != EBADF))
#else
# define STREAM_OK(_s) 1
#endif

#define COLOR_BOLD_RED      "1;31"
#define COLOR_CYAN          "0;36"
#define COLOR_GREEN         "0;32"
#define COLOR_BOLD_BLUE      "1;34"
#define COLOR_BOLD_MAGENTA  "1;35"

#define ERASE_IN_LINE   "\033[K"

/* Nonzero means do output "\033[K" after color open and close.  */
int erase_in_line_flag = 1;
/* Nonzero means do colorize output.  */
int color_flag;

/* These colors are uses for output.  Can be overridden from "MAKE_COLORS". */
const char * color_dir_enter = COLOR_CYAN;
const char * color_dir_leave = COLOR_CYAN;
const char * color_misc_message = COLOR_GREEN;
const char * color_misc_error = COLOR_BOLD_BLUE;
const char * color_misc_fatal = COLOR_BOLD_RED;
const char * color_execution = COLOR_BOLD_MAGENTA;

#define PREVENT_NULL(s)  ((s) ? (s) : "<error>")


typedef struct _char_range_t {
	const char * first;
	const char * after_last;
} char_range_t;

#define RANGE_LEN(range)  (range.after_last - range.first)
#define RANGE_DUP(range)  xstrndup(range.first, RANGE_LEN(range))

//  xxxxx yyyyyyyyyyyyy
//           ^first  |len
//  so xxxx must be equal to the string first ...len ?

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

/* |--| |--| ....
   .member is *char
   */
/* Find the index of a certain string in an array of mapping_def_t instances
   The string is given by delimiting pointers. */
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

/*  Called at init.
 *  If Env. var  MAKE_COLORS is set, redefines these
 *  variables color_misc_error ....
 */
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

  char_range_t name = EMPTY_RANGE; /* delimits the keyword in the EnvVar value */
  char_range_t value = EMPTY_RANGE;
  if (! MAKE_COLORS)
    return;

  /* Example: MAKE_COLORS='erase=no:enter=0;42:leave=0;41:message=0' */
  for (; key_pos != 0; key_pos = colon_pos + 1)
    {
      const char * const assign_pos = strchr(key_pos, '=');
      if (! assign_pos) {
              OS (fatal, NILF, "Assignment ('=') missing in MAKE_COLORS: \"%s\"", key_pos);
      }

      colon_pos = strchr(assign_pos, ':');
      if (! colon_pos)
        colon_pos = assign_pos + strlen(assign_pos);

      /* delimit the keyword & value */
      RANGE_SET(name, key_pos, assign_pos);
      RANGE_SET(value, assign_pos + 1, colon_pos);

      if (RANGE_LEN(name) > 0)
        {
          FIND(valid_names, key, name, name_index);
          if (name_index == -1)
            {
              char * const s = RANGE_DUP(name);
              /* so this works even in out-of-memory, when s is NULL: */
              OS (fatal, NILF, "Invalid name in MAKE_COLORS: \"%s\"", PREVENT_NULL(s));
              free(s);
            }
         }
      else
        OS (fatal, NILF, "Empty name in MAKE_COLORS: \"%s\"", key_pos);

      /* Which kind of statement do we have? */
      if (valid_names[name_index].flag_destination)
        {
          /* Boolean statement */
          int * const destination = valid_names[name_index].flag_destination;
          if (RANGE_LEN(value) <= 0)
            {
              const char * const switch_name = valid_names[name_index].key;
              OS (fatal, NILF, "Empty value for switch \"%s\" in MAKE_COLORS", switch_name);
            }
          else if (RANGE_EQUALS(value, "yes"))
            {
              // DB(DB_VERBOSE, ("Erase in line enabled\n"));
              *destination = 1;
            }
          else if (RANGE_EQUALS(value, "no"))
            {
              // DB(DB_VERBOSE, ("Erase in line disabled\n"));
              *destination = 0;
            }
          else
            {
              /* Invalid (i.e. neither "yes" nor "no") */
              const char * const switch_name = valid_names[name_index].key;
              char * const guilty_part = RANGE_DUP(value);
              OSS (fatal, NILF, "Invalid value for switch \"%s\" in MAKE_COLORS: \"%s\"", switch_name, PREVENT_NULL(guilty_part));
              free(guilty_part);
            }
        }
      else
        {
          /* Colorization statement */
          const int value_is_valid = RANGE_LEN(value) > 0;
          if (value_is_valid)
            {
              // const char * const class_name = valid_names[name_index].key;
              const char * const color = PREVENT_NULL(RANGE_DUP(value));
              const char ** const color_destination = valid_names[name_index].color_destination;
              // DB(DB_VERBOSE, ("Applying color \"%s\" to class \"%s\"\n", color, class_name));
              *color_destination = color;
            }
          else
            {
              const char_range_t guilty_range = { key_pos, colon_pos };
              char * const guilty_part = RANGE_DUP(guilty_range);
              OS(fatal, NILF, "Invalid color mapping in MAKE_COLORS: \"%s\"", PREVENT_NULL(guilty_part));
              free(guilty_part);
            }
        }

      /* Done? */
      if (colon_pos[0] == '\0')
        break;
  }
}

/* Write a string to the current STDOUT or STDERR.  */
static void
_outputs (struct output *out, int is_err, const char *msg)
{
  if (! out || ! out->syncout)
    {
      FILE *f = is_err ? stderr : stdout;
      fputs (msg, f);
      fflush (f);
    }
  else
    {
      int fd = is_err ? out->err : out->out;
      int len = strlen (msg);
      int r;

      EINTRLOOP (r, lseek (fd, 0, SEEK_END));
      while (1)
        {
          EINTRLOOP (r, write (fd, msg, len));
          if (r == len || r <= 0)
            break;
          len -= r;
          msg += r;
        }
    }
}

/* max length of color-set/reset escape sequence: set + reset  sizeof(ERASE_IN_LINE)*/
#define COLOR_MAX_SPACE ((2 + 3 + 1 + 3 ) + ( 3 + 3))

static int start_color(char* buffer, const char * color)
{
  return sprintf(buffer, "\033[%sm%s", color, erase_in_line_flag ? ERASE_IN_LINE : "");
}

static int stop_color(char* buffer)
{
  return sprintf(buffer, "\033[m%s", erase_in_line_flag ? ERASE_IN_LINE : "");
}

/* Write a message indicating that we've just entered or
   left (according to ENTERING) the current directory.  */

static int
log_working_directory (int entering)
{
  static char *buf = NULL;
  static unsigned int len = 0;
  unsigned int need;
  const char *fmt;
  char *p;

  /* Get enough space for the longest possible output.  */
  need = strlen (program) + INTSTR_LENGTH + 2 + 1;
  need += COLOR_MAX_SPACE;
  if (starting_directory)
    need += strlen (starting_directory);

  /* Use entire sentences to give the translators a fighting chance.  */
  if (makelevel == 0)
    if (starting_directory == 0)
      if (entering)
        fmt = _("%s: Entering an unknown directory\n");
      else
        fmt = _("%s: Leaving an unknown directory\n");
    else
      if (entering)
        fmt = _("%s: Entering directory '%s'\n");
      else
        fmt = _("%s: Leaving directory '%s'\n");
  else
    if (starting_directory == 0)
      if (entering)
        fmt = _("%s[%u]: Entering an unknown directory\n");
      else
        fmt = _("%s[%u]: Leaving an unknown directory\n");
    else
      if (entering)
        fmt = _("%s[%u]: Entering directory '%s'\n");
      else
        fmt = _("%s[%u]: Leaving directory '%s'\n");

  need += strlen (fmt);

  if (need > len)
    {
      buf = xrealloc (buf, need);
      len = need;
    }

  /* mmc: now start typing into the buffer: */
  if (color_flag)
    {
    /* fixme:  negative value on error! */
      p = buf + start_color (buf, entering?color_dir_enter:color_dir_leave);
    }
  else
    p = buf;
  if (print_data_base_flag)
    {
      *(p++) = '#';
      *(p++) = ' ';
    }

  if (makelevel == 0)
    {
    if (starting_directory == 0)
      sprintf (p, fmt, program);
    else
      sprintf (p, fmt, program, starting_directory);
    }
  else if (starting_directory == 0)
    sprintf (p, fmt, program, makelevel);
  else
    sprintf (p, fmt, program, makelevel, starting_directory);

  if (color_flag)
    /* we overwrite the newline! */
    /* this is optional, we yes, we have to overwrite: */
    stop_color (buf + strlen(buf) -1);

  /* I'd say stderr! */
  _outputs (NULL, 0, buf);

  return 1;
}

/* Set a file descriptor to be in O_APPEND mode.
   If it fails, just ignore it.  */

static void
set_append_mode (int fd)
{
#if defined(F_GETFL) && defined(F_SETFL) && defined(O_APPEND)
  int flags = fcntl (fd, F_GETFL, 0);
  if (flags >= 0)
    fcntl (fd, F_SETFL, flags | O_APPEND);
#endif
}


#ifndef NO_OUTPUT_SYNC

/* Semaphore for use in -j mode with output_sync. */
static sync_handle_t sync_handle = -1;

#define FD_NOT_EMPTY(_f) ((_f) != OUTPUT_NONE && lseek ((_f), 0, SEEK_END) > 0)

/* Set up the sync handle.  Disables output_sync on error.  */
static int
sync_init ()
{
  int combined_output = 0;

#ifdef WINDOWS32
  if ((!STREAM_OK (stdout) && !STREAM_OK (stderr))
      || (sync_handle = create_mutex ()) == -1)
    {
      perror_with_name ("output-sync suppressed: ", "stderr");
      output_sync = 0;
    }
  else
    {
      combined_output = same_stream (stdout, stderr);
      prepare_mutex_handle_string (sync_handle);
    }

#else
  if (STREAM_OK (stdout))
    {
      struct stat stbuf_o, stbuf_e;

      sync_handle = fileno (stdout);
      combined_output = (fstat (fileno (stdout), &stbuf_o) == 0
                         && fstat (fileno (stderr), &stbuf_e) == 0
                         && stbuf_o.st_dev == stbuf_e.st_dev
                         && stbuf_o.st_ino == stbuf_e.st_ino);
    }
  else if (STREAM_OK (stderr))
    sync_handle = fileno (stderr);
  else
    {
      perror_with_name ("output-sync suppressed: ", "stderr");
      output_sync = 0;
    }
#endif

  return combined_output;
}

/* Support routine for output_sync() */
static void
pump_from_tmp (int from, FILE *to)
{
  static char buffer[8192];

#ifdef WINDOWS32
  int prev_mode;

  /* "from" is opened by open_tmpfd, which does it in binary mode, so
     we need the mode of "to" to match that.  */
  prev_mode = _setmode (fileno (to), _O_BINARY);
#endif

  if (lseek (from, 0, SEEK_SET) == -1)
    perror ("lseek()");

  while (1)
    {
      int len;
      EINTRLOOP (len, read (from, buffer, sizeof (buffer)));
      if (len < 0)
        perror ("read()");
      if (len <= 0)
        break;
      if (fwrite (buffer, len, 1, to) < 1)
        perror ("fwrite()");
    }

#ifdef WINDOWS32
  /* Switch "to" back to its original mode, so that log messages by
     Make have the same EOL format as without --output-sync.  */
  _setmode (fileno (to), prev_mode);
#endif
}

/* Obtain the lock for writing output.  */
static void *
acquire_semaphore (void)
{
  static struct flock fl;

  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start = 0;
  fl.l_len = 1;
  if (fcntl (sync_handle, F_SETLKW, &fl) != -1)
    return &fl;
  perror ("fcntl()");
  return NULL;
}

/* Release the lock for writing output.  */
static void
release_semaphore (void *sem)
{
  struct flock *flp = (struct flock *)sem;
  flp->l_type = F_UNLCK;
  if (fcntl (sync_handle, F_SETLKW, flp) == -1)
    perror ("fcntl()");
}

/* Returns a file descriptor to a temporary file.  The file is automatically
   closed/deleted on exit.  Don't use a FILE* stream.  */
int
output_tmpfd ()
{
  int fd = -1;
  FILE *tfile = tmpfile ();

  if (! tfile)
    pfatal_with_name ("tmpfile");

  /* Create a duplicate so we can close the stream.  */
  fd = dup (fileno (tfile));
  if (fd < 0)
    pfatal_with_name ("dup");

  fclose (tfile);

  set_append_mode (fd);

  return fd;
}

/* Adds file descriptors to the child structure to support output_sync; one
   for stdout and one for stderr as long as they are open.  If stdout and
   stderr share a device they can share a temp file too.
   Will reset output_sync on error.  */
static void
setup_tmpfile (struct output *out)
{
  /* Is make's stdout going to the same place as stderr?  */
  static int combined_output = -1;

  if (combined_output < 0)
    combined_output = sync_init ();

  if (STREAM_OK (stdout))
    {
      int fd = output_tmpfd ();
      if (fd < 0)
        goto error;
      CLOSE_ON_EXEC (fd);
      out->out = fd;
    }

  if (STREAM_OK (stderr))
    {
      if (out->out != OUTPUT_NONE && combined_output)
        out->err = out->out;
      else
        {
          int fd = output_tmpfd ();
          if (fd < 0)
            goto error;
          CLOSE_ON_EXEC (fd);
          out->err = fd;
        }
    }

  return;

  /* If we failed to create a temp file, disable output sync going forward.  */
 error:
  output_close (out);
  output_sync = 0;
}

/* Synchronize the output of jobs in -j mode to keep the results of
   each job together. This is done by holding the results in temp files,
   one for stdout and potentially another for stderr, and only releasing
   them to "real" stdout/stderr when a semaphore can be obtained. */

void
output_dump (struct output *out)
{
  int outfd_not_empty = FD_NOT_EMPTY (out->out);
  int errfd_not_empty = FD_NOT_EMPTY (out->err);

  if (outfd_not_empty || errfd_not_empty)
    {
      int traced = 0;

      /* Try to acquire the semaphore.  If it fails, dump the output
         unsynchronized; still better than silently discarding it.
         We want to keep this lock for as little time as possible.  */
      void *sem = acquire_semaphore ();

      /* Log the working directory for this dump.  */
      if (print_directory_flag && output_sync != OUTPUT_SYNC_RECURSE)
        traced = log_working_directory (1);

      if (outfd_not_empty)
        pump_from_tmp (out->out, stdout);
      if (errfd_not_empty && out->err != out->out)
        pump_from_tmp (out->err, stderr);

      if (traced)
        log_working_directory (0);

      /* Exit the critical section.  */
      if (sem)
        release_semaphore (sem);

      /* Truncate and reset the output, in case we use it again.  */
      if (out->out != OUTPUT_NONE)
        {
          int e;
          lseek (out->out, 0, SEEK_SET);
          EINTRLOOP (e, ftruncate (out->out, 0));
        }
      if (out->err != OUTPUT_NONE && out->err != out->out)
        {
          int e;
          lseek (out->err, 0, SEEK_SET);
          EINTRLOOP (e, ftruncate (out->err, 0));
        }
    }
}
#endif /* NO_OUTPUT_SYNC */


/* Provide support for temporary files.  */

#ifndef HAVE_STDLIB_H
# ifdef HAVE_MKSTEMP
int mkstemp (char *template);
# else
char *mktemp (char *template);
# endif
#endif

FILE *
output_tmpfile (char **name, const char *template)
{
#ifdef HAVE_FDOPEN
  int fd;
#endif

#if defined HAVE_MKSTEMP || defined HAVE_MKTEMP
# define TEMPLATE_LEN   strlen (template)
#else
# define TEMPLATE_LEN   L_tmpnam
#endif
  *name = xmalloc (TEMPLATE_LEN + 1);
  strcpy (*name, template);

#if defined HAVE_MKSTEMP && defined HAVE_FDOPEN
  /* It's safest to use mkstemp(), if we can.  */
  fd = mkstemp (*name);
  if (fd == -1)
    return 0;
  return fdopen (fd, "w");
#else
# ifdef HAVE_MKTEMP
  (void) mktemp (*name);
# else
  (void) tmpnam (*name);
# endif

# ifdef HAVE_FDOPEN
  /* Can't use mkstemp(), but guard against a race condition.  */
  fd = open (*name, O_CREAT|O_EXCL|O_WRONLY, 0600);
  if (fd == -1)
    return 0;
  return fdopen (fd, "w");
# else
  /* Not secure, but what can we do?  */
  return fopen (*name, "w");
# endif
#endif
}


/* This code is stolen from gnulib.
   If/when we abandon the requirement to work with K&R compilers, we can
   remove this (and perhaps other parts of GNU make!) and migrate to using
   gnulib directly.

   This is called only through atexit(), which means die() has already been
   invoked.  So, call exit() here directly.  Apparently that works...?
*/

/* Close standard output, exiting with status 'exit_failure' on failure.
   If a program writes *anything* to stdout, that program should close
   stdout and make sure that it succeeds before exiting.  Otherwise,
   suppose that you go to the extreme of checking the return status
   of every function that does an explicit write to stdout.  The last
   printf can succeed in writing to the internal stream buffer, and yet
   the fclose(stdout) could still fail (due e.g., to a disk full error)
   when it tries to write out that buffered data.  Thus, you would be
   left with an incomplete output file and the offending program would
   exit successfully.  Even calling fflush is not always sufficient,
   since some file systems (NFS and CODA) buffer written/flushed data
   until an actual close call.

   Besides, it's wasteful to check the return value from every call
   that writes to stdout -- just let the internal stream state record
   the failure.  That's what the ferror test is checking below.

   It's important to detect such failures and exit nonzero because many
   tools (most notably 'make' and other build-management systems) depend
   on being able to detect failure in other tools via their exit status.  */

static void
close_stdout (void)
{
  int prev_fail = ferror (stdout);
  int fclose_fail = fclose (stdout);

  if (prev_fail || fclose_fail)
    {
      if (fclose_fail)
        perror_with_name (_("write error: stdout"), "");
      else
        O (error, NILF, _("write error: stdout"));
      exit (EXIT_FAILURE);
    }
}


void
output_init (struct output *out)
{
  if (out)
    {
      out->out = out->err = OUTPUT_NONE;
      out->syncout = !!output_sync;
      return;
    }

  /* Configure this instance of make.  Be sure stdout is line-buffered.  */

#ifdef HAVE_SETVBUF
# ifdef SETVBUF_REVERSED
  setvbuf (stdout, _IOLBF, xmalloc (BUFSIZ), BUFSIZ);
# else  /* setvbuf not reversed.  */
  /* Some buggy systems lose if we pass 0 instead of allocating ourselves.  */
  setvbuf (stdout, 0, _IOLBF, BUFSIZ);
# endif /* setvbuf reversed.  */
#elif HAVE_SETLINEBUF
  setlinebuf (stdout);
#endif  /* setlinebuf missing.  */

  /* Force stdout/stderr into append mode.  This ensures parallel jobs won't
     lose output due to overlapping writes.  */
  set_append_mode (fileno (stdout));
  set_append_mode (fileno (stderr));

#ifdef HAVE_ATEXIT
  if (STREAM_OK (stdout))
    atexit (close_stdout);
#endif
}

void
output_close (struct output *out)
{
  if (! out)
    {
      if (stdio_traced)
        log_working_directory (0);
      return;
    }

#ifndef NO_OUTPUT_SYNC
  output_dump (out);
#endif

  if (out->out >= 0)
    close (out->out);
  if (out->err >= 0 && out->err != out->out)
    close (out->err);

  output_init (out);
}

/* We're about to generate output: be sure it's set up.  */
void
output_start ()
{
#ifndef NO_OUTPUT_SYNC
  /* If we're syncing output make sure the temporary file is set up.  */
  if (output_context && output_context->syncout)
    if (! OUTPUT_ISSET(output_context))
      setup_tmpfile (output_context);
#endif

  /* If we're not syncing this output per-line or per-target, make sure we emit
     the "Entering..." message where appropriate.  */
  if (output_sync == OUTPUT_SYNC_NONE || output_sync == OUTPUT_SYNC_RECURSE)
    if (! stdio_traced && print_directory_flag)
      stdio_traced = log_working_directory (1);
}

void
outputs (int is_err, const char *msg)
{
  if (! msg || *msg == '\0')
    return;

  output_start ();

  _outputs (output_context, is_err, msg);
}


static struct fmtstring
  {
    char *buffer;
    size_t size;
  } fmtbuf = { NULL, 0 };

static char *
get_buffer (size_t need)
{
  /* Make sure we have room.  */
  if (need > fmtbuf.size)
    {
      fmtbuf.size += need * 2;
      fmtbuf.buffer = xrealloc (fmtbuf.buffer, fmtbuf.size);
    }

  fmtbuf.buffer[need] = '\0';

  return fmtbuf.buffer;
}

/* Print a message on stdout.  */

void
message (int prefix, size_t len, const char *fmt, ...)
{
  va_list args;
  char *p;

  len += strlen (fmt) + strlen (program) + INTSTR_LENGTH + 4 + 1 + 1;
  if (color_flag)
    len += COLOR_MAX_SPACE;
  p = get_buffer (len);

  if (color_flag) {
    /* color_misc_message color_execution */
    p += start_color(p,color_misc_message);
  }

  if (prefix)
    {
      if (makelevel == 0)
        sprintf (p, "%s: ", program);
      else
        sprintf (p, "%s[%u]: ", program, makelevel);
      p += strlen (p);
    }

  va_start (args, fmt);
  p += vsprintf (p, fmt, args);
  va_end (args);

  if (color_flag)
    p += stop_color (p);

  strcat (p, "\n");

  assert (fmtbuf.buffer[len] == '\0');
  outputs (0, fmtbuf.buffer);
}

/* Print an error message.  */

void
error (const gmk_floc *flocp, size_t len, const char *fmt, ...)
{
  va_list args;
  char *p;

  len += (strlen (fmt) + strlen (program)
          + (flocp && flocp->filenm ? strlen (flocp->filenm) : 0)
          + INTSTR_LENGTH + 4 + 1 + 1);
  if (color_flag)
    len += COLOR_MAX_SPACE;

  p = get_buffer (len);

  if (color_flag) {
    p += start_color(p,color_misc_error);
  }
  if (flocp && flocp->filenm)
    sprintf (p, "%s:%lu: ", flocp->filenm, flocp->lineno);
  else if (makelevel == 0)
    sprintf (p, "%s: ", program);
  else
    sprintf (p, "%s[%u]: ", program, makelevel);
  p += strlen (p);

  va_start (args, fmt);
  p += vsprintf (p, fmt, args);
  va_end (args);

  if (color_flag)
    p += stop_color (p);

  strcat (p, "\n");

  assert (fmtbuf.buffer[len] == '\0');
  outputs (1, fmtbuf.buffer);
}

/* Print an error message and exit.  */

void
fatal (const gmk_floc *flocp, size_t len, const char *fmt, ...)
{
  va_list args;
  const char *stop = _(".  Stop.\n");
  char *p;

  len += (strlen (fmt) + strlen (program)
          + (flocp && flocp->filenm ? strlen (flocp->filenm) : 0)
          + INTSTR_LENGTH + 8 + strlen (stop) + 1);

  if (color_flag)
    len += COLOR_MAX_SPACE;

  p = get_buffer (len);

  if (color_flag) {
    p += start_color(p,color_misc_fatal);
  }


  if (flocp && flocp->filenm)
    sprintf (p, "%s:%lu: *** ", flocp->filenm, flocp->lineno);
  else if (makelevel == 0)
    sprintf (p, "%s: *** ", program);
  else
    sprintf (p, "%s[%u]: *** ", program, makelevel);
  p += strlen (p);

  va_start (args, fmt);
  p += vsprintf (p, fmt, args);
  va_end (args);

  strcat (p, stop);
  p += strlen (stop);

  if (color_flag)
    p += stop_color (p);

  assert (fmtbuf.buffer[len] == '\0');
  outputs (1, fmtbuf.buffer);

  die (2);
}

/* Print an error message from errno.  */

void
perror_with_name (const char *str, const char *name)
{
  const char *err = strerror (errno);
  OSSS (error, NILF, _("%s%s: %s"), str, name, err);
}

/* Print an error message from errno and exit.  */

void
pfatal_with_name (const char *name)
{
  const char *err = strerror (errno);
  OSS (fatal, NILF, _("%s: %s"), name, err);

  /* NOTREACHED */
}
