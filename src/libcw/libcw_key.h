/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_KEY
#define H_LIBCW_KEY





#include <stdbool.h>





#include "libcw2.h"





void cw_key_tk_set_value_internal(cw_key_t *key, int key_state);

int  cw_key_ik_update_graph_state_internal(cw_key_t *keyer);
void cw_key_ik_increment_timer_internal(cw_key_t *keyer, int usecs);
void cw_key_ik_get_paddle_latches_internal(cw_key_t *key, int *dot_paddle_latch_state, int *dash_paddle_latch_state);
bool cw_key_ik_is_busy_internal(cw_key_t *key);
void cw_key_ik_reset_internal(cw_key_t *key);

void cw_key_sk_reset_internal(cw_key_t *key);





#endif /* #ifndef H_LIBCW_KEY */
