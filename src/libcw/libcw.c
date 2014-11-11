/*
  Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
  Copyright (C) 2011-2014  Kamil Ignacak (acerion@wp.pl)

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/



#include "config.h"


#include <unistd.h>
#include <stdlib.h>

#include <errno.h>
#include <assert.h>
#include <stdbool.h>
#include <math.h>  /* sqrt() */



#if (defined(__unix__) || defined(unix)) && !defined(USG)
# include <sys/param.h>
#endif

#if defined(HAVE_STRING_H)
# include <string.h>
#endif

#if defined(HAVE_STRINGS_H)
# include <strings.h>
#endif





#if defined(BSD)
# define ERR_NO_SUPPORT EPROTONOSUPPORT
#else
# define ERR_NO_SUPPORT EPROTO
#endif





#include "libcw.h"
#include "libcw_internal.h"
#include "libcw_data.h"
#include "libcw_utils.h"
#include "libcw_gen.h"
#include "libcw_signal.h"
#include "libcw_debug.h"








/* ******************************************************************** */
/*                    Section:Global variables                          */
/* ******************************************************************** */

extern cw_gen_t *cw_generator;
extern cw_rec_t  cw_receiver;

extern cw_debug_t cw_debug_object;
extern cw_debug_t cw_debug_object_ev;
extern cw_debug_t cw_debug_object_dev;
