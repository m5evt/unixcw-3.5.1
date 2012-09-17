/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/


#ifndef H_CW_ALSA
#define H_CW_ALSA

#if defined(LIBCW_WITH_ALSA)


#include "libcw_internal.h"



int  cw_alsa_configure(cw_gen_t *gen, const char *device);
void cw_alsa_drop(cw_gen_t *gen);



#endif // #if defined(LIBCW_WITH_ALSA)

#endif // #ifndef H_CW_ALSA
