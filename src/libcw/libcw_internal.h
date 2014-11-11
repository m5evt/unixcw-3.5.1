/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_INTERNAL
#define H_LIBCW_INTERNAL





#include <stdbool.h>

#include "libcw.h"
#include "libcw_rec.h"




/* From libcw.c, needed in libcw_key.c. */
void cw_sync_parameters_internal(cw_gen_t *gen, cw_rec_t *rec);
void cw_finalization_schedule_internal(void);

/* From libcw.c, needed in libcw_signal.c. */
void cw_finalization_cancel_internal(void);




#endif /* #ifndef H_LIBCW_INTERNAL */
