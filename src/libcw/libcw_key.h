/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License, version 2 or later.
*/

#ifndef H_LIBCW_KEY
#define H_LIBCW_KEY




#include <stdbool.h>





#include "libcw_gen.h"
#include "libcw_rec.h"





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



typedef void (* cw_key_callback_t)(volatile struct timeval * timestamp, int key_state, void * arg);

struct cw_key_struct {
	/* Straight key and iambic keyer needs a generator to produce
	   a sound on "Key Down" events. Maybe we don't always need a
	   sound, but sometimes we do want to have it.

	   Additionally iambic keyer needs a generator for timing
	   purposes. Even if we came up with a different mechanism for
	   timing the key, we still would need to use generator to
	   produce a sound - then we would have a kind of
	   duplication. So let's always use a generator. Sometimes for
	   timing iambic keyer, sometimes for generating sound, but
	   always the same generator.

	   In any case - a key needs to have access to a generator
	   (but a generator doesn't need a key). This is why the key
	   data type has a "generator" field, not the other way
	   around. */
	cw_gen_t *gen;


	/* There should be a binding between key and a receiver.

	   The receiver can get it's properly formed input data (key
	   down/key up events) from any source (any code that can call
	   receiver's 'mark_begin()' and 'mark_end()' functions), so
	   receiver is independent from key. On the other hand the key
	   without receiver is rather useless. Therefore I think that
	   the key should contain reference to a receiver, not the
	   other way around.

	   There may be one purpose of having a key without libcw
	   receiver: iambic keyer mechanism may be used to ensure a
	   functioning iambic keyer, but there may be a
	   different/external/3-rd party receiver that is
	   controlled/fed by cw_key_t->key_callback_func function. */
	cw_rec_t * rec;

	/* External "on key state change" callback function and its
	   argument.

	   It may be useful for a client to have this library control
	   an external keying device, for example, an oscillator, or a
	   transmitter.  Here is where we keep the address of a
	   function that is passed to us for this purpose, and a void*
	   argument for it. */
	cw_key_callback_t key_callback_func;
	void *key_callback_arg;


	/* Straight key. */
	struct {
		int key_value;    /* Open/Closed, Space/Mark, NoSound/Sound. */
	} sk;


	/* Iambic keyer.  The keyer functions maintain the current
	   known state of the paddles, and latch false-to-true
	   transitions while busy, to form the iambic effect.  For
	   Curtis mode B, the keyer also latches any point where both
	   paddle values are CLOSED at the same time. */
	struct {
		int graph_state;       /* State of iambic keyer state machine. */
		int key_value;         /* CW_KEY_STATE_OPEN or CW_KEY_STATE_CLOSED (Space/Mark, NoSound/Sound). */

		bool dot_paddle;       /* Dot paddle value. CW_KEY_STATE_OPEN or CW_KEY_STATE_CLOSED. */
		bool dash_paddle;      /* Dash paddle value. CW_KEY_STATE_OPEN or CW_KEY_STATE_CLOSED. */

		bool dot_latch;        /* Dot false->true latch */
		bool dash_latch;       /* Dash false->true latch */

		/* Iambic keyer "Curtis" mode A/B selector.  Mode A and mode B timings
		   differ slightly, and some people have a preference for one or the other.
		   Mode A is a bit less timing-critical, so we'll make that the default. */
		bool curtis_mode_b;

		bool curtis_b_latch;   /* Curtis Dot&Dash latch */

		bool lock;             /* FIXME: describe why we need this flag. */
	} ik;


	/* Tone-queue key. */
	struct {
		int key_value;    /* Open/Closed, Space/Mark, NoSound/Sound. */
	} tk;

	/* Every key event needs to have a timestamp. */
	struct timeval timer;
};





typedef struct cw_key_struct cw_key_t;





void cw_key_tk_set_value_internal(volatile cw_key_t * key, int key_state);

int  cw_key_ik_update_graph_state_internal(volatile cw_key_t * key);
void cw_key_ik_increment_timer_internal(volatile cw_key_t * key, int usecs);


void cw_key_ik_get_paddle_latches_internal(volatile cw_key_t * key, int * dot_paddle_latch_state, int * dash_paddle_latch_state);
bool cw_key_ik_is_busy_internal(const volatile cw_key_t * key);
void cw_key_ik_reset_internal(volatile cw_key_t * key);

void cw_key_sk_reset_internal(volatile cw_key_t * key);



#endif // #ifndef H_LIBCW_KEY
