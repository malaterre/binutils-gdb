/* Serial interface for local (hardwired) serial ports on Un*x like systems
   Copyright 1992, 1993 Free Software Foundation, Inc.

This file is part of GDB.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include "defs.h"
#include "serial.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>

#if !defined (HAVE_TERMIOS) && !defined (HAVE_TERMIO) && !defined (HAVE_SGTTY)
#define HAVE_SGTTY
#endif

#ifdef HAVE_TERMIOS
#include <termios.h>
#include <unistd.h>

struct hardwire_ttystate
{
  struct termios termios;
};
#endif

#ifdef HAVE_TERMIO
#include <termio.h>

struct hardwire_ttystate
{
  struct termio termio;
};
#endif

#ifdef HAVE_SGTTY
#include <sgtty.h>

struct hardwire_ttystate
{
  struct sgttyb sgttyb;
};
#endif

static int hardwire_open PARAMS ((serial_t scb, const char *name));
static void hardwire_raw PARAMS ((serial_t scb));
static int wait_for PARAMS ((serial_t scb, int timeout));
static int hardwire_readchar PARAMS ((serial_t scb, int timeout));
static int rate_to_code PARAMS ((int rate));
static int hardwire_setbaudrate PARAMS ((serial_t scb, int rate));
static int hardwire_write PARAMS ((serial_t scb, const char *str, int len));
static void hardwire_restore PARAMS ((serial_t scb));
static void hardwire_close PARAMS ((serial_t scb));
static int get_tty_state PARAMS ((serial_t scb, struct hardwire_ttystate *state));
static int set_tty_state PARAMS ((serial_t scb, struct hardwire_ttystate *state));
static serial_ttystate hardwire_get_tty_state PARAMS ((serial_t scb));
static int hardwire_set_tty_state PARAMS ((serial_t scb, serial_ttystate state));

/* Open up a real live device for serial I/O */

static int
hardwire_open(scb, name)
     serial_t scb;
     const char *name;
{
  scb->fd = open (name, O_RDWR);
  if (scb->fd < 0)
    return -1;

  return 0;
}

static int
get_tty_state(scb, state)
     serial_t scb;
     struct hardwire_ttystate *state;
{
#ifdef HAVE_TERMIOS
  return tcgetattr(scb->fd, &state->termios);
#endif

#ifdef HAVE_TERMIO
  return ioctl (scb->fd, TCGETA, &state->termio);
#endif

#ifdef HAVE_SGTTY
  return ioctl (scb->fd, TIOCGETP, &state->sgttyb);
#endif
}

static int
set_tty_state(scb, state)
     serial_t scb;
     struct hardwire_ttystate *state;
{
  int err;

#ifdef HAVE_TERMIOS
  return tcsetattr(scb->fd, TCSANOW, &state->termios);
#endif

#ifdef HAVE_TERMIO
  return ioctl (scb->fd, TCSETA, &state->termio);
#endif

#ifdef HAVE_SGTTY
  return ioctl (scb->fd, TIOCSETP, &state->sgttyb);
#endif
}

static serial_ttystate
hardwire_get_tty_state(scb)
     serial_t scb;
{
  struct hardwire_ttystate *state;

  state = (struct hardwire_ttystate *)xmalloc(sizeof *state);

  if (get_tty_state(scb, state))
    return NULL;

  return (serial_ttystate)state;
}

static int
hardwire_set_tty_state(scb, ttystate)
     serial_t scb;
     serial_ttystate ttystate;
{
  struct hardwire_ttystate *state;

  state = (struct hardwire_ttystate *)ttystate;

  return set_tty_state(scb, state);
}

static void
hardwire_raw(scb)
     serial_t scb;
{
  struct hardwire_ttystate state;

  if (get_tty_state(scb, &state))
    fprintf(stderr, "get_tty_state failed: %s\n", safe_strerror(errno));

#ifdef HAVE_TERMIOS
  state.termios.c_iflag = 0;
  state.termios.c_oflag = 0;
  state.termios.c_lflag = 0;
  state.termios.c_cflag &= ~(CSIZE|PARENB);
  state.termios.c_cflag |= CS8;
  state.termios.c_cc[VMIN] = 0;
  state.termios.c_cc[VTIME] = 0;
#endif

#ifdef HAVE_TERMIO
  state.termio.c_iflag = 0;
  state.termio.c_oflag = 0;
  state.termio.c_lflag = 0;
  state.termio.c_cflag &= ~(CSIZE|PARENB);
  state.termio.c_cflag |= CS8;
  state.termio.c_cc[VMIN] = 0;
  state.termio.c_cc[VTIME] = 0;
#endif

#ifdef HAVE_SGTTY
  state.sgttyb.sg_flags |= RAW | ANYP;
  state.sgttyb.sg_flags &= ~(CBREAK | ECHO);
#endif

  scb->current_timeout = 0;

  if (set_tty_state (scb, &state))
    fprintf(stderr, "set_tty_state failed: %s\n", safe_strerror(errno));
}

/* Wait for input on scb, with timeout seconds.  Returns 0 on success,
   otherwise SERIAL_TIMEOUT or SERIAL_ERROR.

   For termio{s}, we actually just setup VTIME if necessary, and let the
   timeout occur in the read() in hardwire_read().
 */

static int
wait_for(scb, timeout)
     serial_t scb;
     int timeout;
{
  int numfds;

#ifdef HAVE_SGTTY
  struct timeval tv;
  fd_set readfds;

  FD_ZERO (&readfds);

  tv.tv_sec = timeout;
  tv.tv_usec = 0;

  FD_SET(scb->fd, &readfds);

  while (1)
    {
      if (timeout >= 0)
	numfds = select(scb->fd+1, &readfds, 0, 0, &tv);
      else
	numfds = select(scb->fd+1, &readfds, 0, 0, 0);

      if (numfds <= 0)
	if (numfds == 0)
	  return SERIAL_TIMEOUT;
	else if (errno == EINTR)
	  continue;
	else
	  return SERIAL_ERROR;	/* Got an error from select or poll */

      return 0;
    }

#endif	/* HAVE_SGTTY */

#if defined HAVE_TERMIO || defined HAVE_TERMIOS
  if (timeout == scb->current_timeout)
    return 0;

  {
    struct hardwire_ttystate state;

    if (get_tty_state(scb, &state))
      fprintf(stderr, "get_tty_state failed: %s\n", safe_strerror(errno));

#ifdef HAVE_TERMIOS
    state.termios.c_cc[VTIME] = timeout * 10;
#endif

#ifdef HAVE_TERMIO
    state.termio.c_cc[VTIME] = timeout * 10;
#endif

    scb->current_timeout = timeout;

    if (set_tty_state (scb, &state))
      fprintf(stderr, "set_tty_state failed: %s\n", safe_strerror(errno));

    return 0;
  }
#endif	/* HAVE_TERMIO || HAVE_TERMIOS */
}

/* Read a character with user-specified timeout.  TIMEOUT is number of seconds
   to wait, or -1 to wait forever.  Use timeout of 0 to effect a poll.  Returns
   char if successful.  Returns -2 if timeout expired, EOF if line dropped
   dead, or -3 for any other error (see errno in that case). */

static int
hardwire_readchar(scb, timeout)
     serial_t scb;
     int timeout;
{
  int status;

  if (scb->bufcnt-- > 0)
    return *scb->bufp++;

  status = wait_for(scb, timeout);

  if (status < 0)
    return status;

  scb->bufcnt = read(scb->fd, scb->buf, BUFSIZ);

  if (scb->bufcnt <= 0)
    if (scb->bufcnt == 0)
      return SERIAL_TIMEOUT;	/* 0 chars means timeout [may need to
				   distinguish between EOF & timeouts
				   someday] */
    else
      return SERIAL_ERROR;	/* Got an error from read */

  scb->bufcnt--;
  scb->bufp = scb->buf;
  return *scb->bufp++;
}

#ifndef B19200
#define B19200 EXTA
#endif

#ifndef B38400
#define B38400 EXTB
#endif

/* Translate baud rates from integers to damn B_codes.  Unix should
   have outgrown this crap years ago, but even POSIX wouldn't buck it.  */

static struct
{
  int rate;
  int code;
}
baudtab[] =
{
  {50, B50},
  {75, B75},
  {110, B110},
  {134, B134},
  {150, B150},
  {200, B200},
  {300, B300},
  {600, B600},
  {1200, B1200},
  {1800, B1800},
  {2400, B2400},
  {4800, B4800},
  {9600, B9600},
  {19200, B19200},
  {38400, B38400},
  {-1, -1},
};

static int 
rate_to_code(rate)
     int rate;
{
  int i;

  for (i = 0; baudtab[i].rate != -1; i++)
    if (rate == baudtab[i].rate)  
      return baudtab[i].code;

  return -1;
}

static int
hardwire_setbaudrate(scb, rate)
     serial_t scb;
     int rate;
{
  struct hardwire_ttystate state;

  if (get_tty_state(scb, &state))
    return -1;

#ifdef HAVE_TERMIOS
  cfsetospeed (&state.termios, rate_to_code (rate));
  cfsetispeed (&state.termios, rate_to_code (rate));
#endif

#ifdef HAVE_TERMIO
#ifndef CIBAUD
#define CIBAUD CBAUD
#endif

  state.termio.c_cflag &= ~(CBAUD | CIBAUD);
  state.termio.c_cflag |= rate_to_code (rate);
#endif

#ifdef HAVE_SGTTY
  state.sgttyb.sg_ispeed = rate_to_code (rate);
  state.sgttyb.sg_ospeed = rate_to_code (rate);
#endif

  return set_tty_state (scb, &state);
}

static int
hardwire_write(scb, str, len)
     serial_t scb;
     const char *str;
     int len;
{
  int cc;

  while (len > 0)
    {
      cc = write(scb->fd, str, len);

      if (cc < 0)
	return 1;
      len -= cc;
      str += cc;
    }
  return 0;
}

static void
hardwire_close(scb)
     serial_t scb;
{
  if (scb->fd < 0)
    return;

  close(scb->fd);
  scb->fd = -1;
}

static struct serial_ops hardwire_ops =
{
  "hardwire",
  0,
  hardwire_open,
  hardwire_close,
  hardwire_readchar,
  hardwire_write,
  hardwire_raw,
  hardwire_get_tty_state,
  hardwire_set_tty_state,
  hardwire_setbaudrate,
};

void
_initialize_ser_hardwire ()
{
  serial_add_interface (&hardwire_ops);
}
