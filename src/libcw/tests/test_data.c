/*
 * Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
 * Copyright (C) 2011-2019  Kamil Ignacak (acerion@wp.pl)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */




#include <stdio.h>




/* Various static pieces of data to be used in tests. */




const char * test_valid_representations[] = { ".-.-.-",
					      ".-",
					      "---",
					      "...-",

					      NULL }; /* Guard. */

const char * test_invalid_representations[] = { "INVALID",
						"_._T",
						"_.A_.",
						"S-_-",
						"_._", /* This does not represent a valid letter/digit. */
						"-_-", /* This does not represent a valid letter/digit. */

						NULL }; /* Guard. */
