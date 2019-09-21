#ifndef _LIBCW_REC_INTERNAL_H_
#define _LIBCW_REC_INTERNAL_H_




#include <stdbool.h>



#include "libcw_rec.h"
#include "libcw_utils.h"




/* Internal functions of this module, exposed to unit tests code. */




/* Receive and identify a mark. */
CW_STATIC_FUNC int cw_rec_identify_mark_internal(cw_rec_t * rec, int mark_len, char *representation);

/* Functions handling receiver statistics. */
CW_STATIC_FUNC void   cw_rec_update_stats_internal(cw_rec_t * rec, stat_type_t type, int len);
CW_STATIC_FUNC double cw_rec_get_stats_internal(cw_rec_t * rec, stat_type_t type);

CW_STATIC_FUNC void cw_rec_poll_representation_eoc_internal(cw_rec_t * rec, int space_len, char * representation, bool * is_end_of_word, bool * is_error);
CW_STATIC_FUNC void cw_rec_poll_representation_eow_internal(cw_rec_t * rec, char * representation, bool * is_end_of_word, bool * is_error);




#endif /* #ifndef _LIBCW_REC_INTERNAL_H_ */
