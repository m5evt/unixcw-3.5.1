/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_KEY
#define H_LIBCW_KEY





#include <stdbool.h>
#include <sys/time.h>

#include "libcw_gen.h"





/* Iambic keyer status.  The keyer functions maintain the current
   known state of the paddles, and latch false-to-true transitions
   while busy, to form the iambic effect.  For Curtis mode B, the
   keyer also latches any point where both paddle states are true at
   the same time. */
typedef struct cw_iambic_keyer_struct {
	int graph_state;      /* State of iambic keyer state machine. */
	int key_value;        /* Open/Closed, Space/Mark, NoSound/Sound. */

	bool dot_paddle;      /* Dot paddle state */
	bool dash_paddle;     /* Dash paddle state */

	bool dot_latch;       /* Dot false->true latch */
	bool dash_latch;      /* Dash false->true latch */

	/* Iambic keyer "Curtis" mode A/B selector.  Mode A and mode B timings
	   differ slightly, and some people have a preference for one or the other.
	   Mode A is a bit less timing-critical, so we'll make that the default. */
	bool curtis_mode_b;

	bool curtis_b_latch;  /* Curtis Dot&&Dash latch */

	bool lock;            /* FIXME: describe why we need this flag. */

	struct timeval *timer; /* Timer for receiving of iambic keying, owned by client code. */

	/* Generator associated with the keyer. Should never be NULL
	   as iambic keyer *needs* a generator to function
	   properly. Set using
	   cw_iambic_keyer_register_generator_internal(). */
	cw_gen_t *gen;

} cw_iambic_keyer_t;





/* Straight key data type. */
typedef struct cw_straight_key_struct {
	int key_value;        /* Open/Closed, Space/Mark, NoSound/Sound. */
} cw_straight_key_t;





/* KS stands for Keyer State. */
enum {
	KS_IDLE,
	KS_IN_DOT_A,
	KS_IN_DASH_A,
	KS_AFTER_DOT_A,
	KS_AFTER_DASH_A,
	KS_IN_DOT_B,
	KS_IN_DASH_B,
	KS_AFTER_DOT_B,
	KS_AFTER_DASH_B
};



int  cw_iambic_keyer_update_graph_state_internal(cw_iambic_keyer_t *keyer, cw_gen_t *gen);
void cw_iambic_keyer_increment_timer_internal(cw_iambic_keyer_t *keyer, int usecs);
void cw_iambic_keyer_register_generator_internal(cw_iambic_keyer_t *keyer, cw_gen_t *gen);

void cw_key_set_state_internal(int key_state);





#endif // #ifndef H_LIBCW_KEY
