/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/


#ifndef H_CW_NULL
#define H_CW_NULL


#include "libcw_internal.h"


bool cw_is_null_possible(const char *device);
int  cw_null_configure(cw_gen_t *gen, const char *device);
void cw_null_write(cw_gen_t *gen, cw_tone_t *tone);


#endif //#ifndef H_CW_NULL
