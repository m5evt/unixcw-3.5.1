#ifndef _LIBCW_2_H_
#define _LIBCW_2_H_




/* Basic generator functions. */
cw_gen_t * cw_gen_new(int audio_system, const char * device);
void       cw_gen_delete(cw_gen_t ** gen);
int        cw_gen_stop(cw_gen_t * gen);
int        cw_gen_start(cw_gen_t * gen);


/* Setters of generator's basic parameters. */
int cw_gen_set_speed(cw_gen_t * gen, int new_value);
int cw_gen_set_frequency(cw_gen_t * gen, int new_value);
int cw_gen_set_volume(cw_gen_t * gen, int new_value);
int cw_gen_set_gap(cw_gen_t * gen, int new_value);
int cw_gen_set_weighting(cw_gen_t * gen, int new_value);


/* Getters of generator's basic parameters. */
int cw_gen_get_speed(const cw_gen_t * gen);
int cw_gen_get_frequency(const cw_gen_t * gen);
int cw_gen_get_volume(const cw_gen_t * gen);
int cw_gen_get_gap(const cw_gen_t * gen);
int cw_gen_get_weighting(const cw_gen_t * gen);

int cw_gen_enqueue_string(cw_gen_t * gen, const char * string);




cw_key_t * cw_key_new(void);
void cw_key_delete(cw_key_t ** key);

void cw_key_register_keying_callback(volatile cw_key_t * key, void (* callback_func)(void *, int), void * callback_arg);
void cw_key_register_generator(volatile cw_key_t * key, cw_gen_t * gen);
void cw_key_register_receiver(volatile cw_key_t * key, cw_rec_t * rec);

void cw_key_ik_enable_curtis_mode_b(volatile cw_key_t * key);
void cw_key_ik_disable_curtis_mode_b(volatile cw_key_t * key);
bool cw_key_ik_get_curtis_mode_b(const volatile cw_key_t * key);
int  cw_key_ik_notify_paddle_event(volatile cw_key_t * key, int dot_paddle_state, int dash_paddle_state);
int  cw_key_ik_notify_dash_paddle_event(volatile cw_key_t * key, int dash_paddle_state);
int  cw_key_ik_notify_dot_paddle_event(volatile cw_key_t * key, int dot_paddle_state);
void cw_key_ik_get_paddles(const volatile cw_key_t * key, int * dot_paddle_state, int * dash_paddle_state);
int  cw_key_ik_wait_for_element(const volatile cw_key_t * key);
int  cw_key_ik_wait_for_keyer(volatile cw_key_t * key);

int  cw_key_sk_get_value(const volatile cw_key_t * key);
bool cw_key_sk_is_busy(volatile cw_key_t * key);
int  cw_key_sk_notify_event(volatile cw_key_t * key, int key_state);




/* Creator and destructor. */
cw_rec_t * cw_rec_new(void);
void       cw_rec_delete(cw_rec_t ** rec);


/* Helper receive functions. */
int  cw_rec_poll_character(cw_rec_t *rec, const struct timeval *timestamp, char *c, bool *is_end_of_word, bool *is_error);
void cw_rec_reset_state(cw_rec_t *rec);

/* Setters of receiver's essential parameters. */
int  cw_rec_set_speed(cw_rec_t * rec, int new_value);
int  cw_rec_set_tolerance(cw_rec_t * rec, int new_value);
int  cw_rec_set_gap(cw_rec_t * rec, int new_value);
int  cw_rec_set_noise_spike_threshold(cw_rec_t * rec, int new_value);
void cw_rec_set_adaptive_mode_internal(cw_rec_t *rec, bool adaptive);

/* Getters of receiver's essential parameters. */
float cw_rec_get_speed(const cw_rec_t *rec);
int   cw_rec_get_tolerance(const cw_rec_t * rec);
/* int   cw_rec_get_gap_internal(cw_rec_t *rec); */
int   cw_rec_get_noise_spike_threshold(const cw_rec_t * rec);
bool  cw_rec_get_adaptive_mode(const cw_rec_t * rec);

void cw_rec_reset_receive_statistics(cw_rec_t * rec);

/* Main receive functions. */
int cw_rec_mark_begin(cw_rec_t * rec, const struct timeval * timestamp);
int cw_rec_mark_end(cw_rec_t * rec, const struct timeval * timestamp);
int cw_rec_add_mark(cw_rec_t * rec, const struct timeval * timestamp, char mark);


/* Helper receive functions. */
int  cw_rec_poll_representation(cw_rec_t * rec, const struct timeval * timestamp, char * representation, bool * is_end_of_word, bool * is_error);



#endif /* #ifndef _LIBCW_2_H_ */
