/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/


#ifndef H_CW_PA
#define H_CW_PA

#if defined(LIBCW_WITH_PULSEAUDIO)


#include <pulse/simple.h>
#include <pulse/error.h>





typedef struct cw_pa_data_struct {
	pa_simple *s;       /* audio handle */
	pa_sample_spec ss;  /* sample specification */
	pa_usec_t latency_usecs;

	pa_buffer_attr ba;
} cw_pa_data_t;

#include "libcw_internal.h"
//typedef struct cw_gen_struct cw_gen_t;


int  cw_pa_configure(cw_gen_t *gen, const char *device);


#endif // #ifdef LIBCW_WITH_PULSEAUDIO

#endif // #ifndef H_CW_PA
