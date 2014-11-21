/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_KEY
#define H_LIBCW_KEY





#include "libcw_gen.h"





/* Forward declaration of data type. */
struct cw_key_struct;
typedef struct cw_key_struct cw_key_t;





int  cw_key_ik_update_graph_state_internal(volatile cw_key_t *keyer);
void cw_key_ik_increment_timer_internal(volatile cw_key_t *keyer, int usecs);

void cw_key_tk_set_value_internal(volatile cw_key_t *key, int key_state);

void cw_key_register_generator_internal(volatile cw_key_t *key, cw_gen_t *gen);





#endif // #ifndef H_LIBCW_KEY
