/* vi: set ts=2 shiftwidth=2 expandtab:
 *
 * Copyright (C) 2001-2006  Simon Baldwin (simon_baldwin@yahoo.com)
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef _CWMEMORY_H
#define _CWMEMORY_H

#if defined(__cplusplus)
extern "C" {
#endif

void *safe_malloc (size_t size);
void *safe_realloc (void *ptr, size_t size);
char *safe_strdup (const char *s);

#if defined(__cplusplus)
}
#endif
#endif  /* _CWMEMORY_H */
