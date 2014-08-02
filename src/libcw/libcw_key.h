/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_KEY
#define H_LIBCW_KEY





#include <stdbool.h>
#include <sys/time.h>

#include "libcw_gen.h"
#include "libcw_tq.h"




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
	   as iambic keyer *needs* a generator to function properly
	   (and to generate audible tones). Set using
	   cw_iambic_keyer_register_generator_internal(). */
	cw_gen_t *gen;

} cw_iambic_keyer_t;





/* Straight key data type. */
typedef struct cw_straight_key_struct {
	int key_value;        /* Open/Closed, Space/Mark, NoSound/Sound. */

	/* Generator associated with the key. Should never be NULL as
	   straight key needs a generator to generate audible
	   tones. Set using
	   cw_straight_key_register_generator_internal(). */
	cw_gen_t *gen;
} cw_straight_key_t;





typedef struct cw_tqkey_struct {
	int key_value;        /* Open/Closed, Space/Mark, NoSound/Sound. */

	/* Tone queue associated with a tq-key. */
	cw_tone_queue_t *tq;
} cw_tqkey_t;





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



int  cw_iambic_keyer_update_graph_state_internal(cw_iambic_keyer_t *keyer);
void cw_iambic_keyer_increment_timer_internal(cw_iambic_keyer_t *keyer, int usecs);
void cw_iambic_keyer_register_generator_internal(cw_iambic_keyer_t *keyer, cw_gen_t *gen);

void cw_straight_key_register_generator_internal(volatile cw_straight_key_t *key, cw_gen_t *gen);

void cw_tqkey_set_value_internal(cw_tqkey_t *tqkey, int key_state);
void cw_tqkey_register_tone_queue_internal(cw_tqkey_t *tqkey, cw_tone_queue_t *tq);




#endif // #ifndef H_LIBCW_KEY
