/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_REC
#define H_LIBCW_REC





/* Dot length magic number.

   From PARIS calibration, 1 dot length [us] = 1200000 / speed [wpm].

   This variable is used in generator code as well. */
enum { CW_DOT_CALIBRATION = 1200000 };





/* Forward declaration of a data type. */
struct cw_rec_struct;
typedef struct cw_rec_struct cw_rec_t;





/* Main receive functions. */
int cw_rec_mark_begin_internal(cw_rec_t *rec, const struct timeval *timestamp);
int cw_rec_mark_end_internal(cw_rec_t *rec, const struct timeval *timestamp);
int cw_rec_add_mark_internal(cw_rec_t *rec, const struct timeval *timestamp, char mark);

/* Helper receive functions. */
int  cw_rec_poll_representation_internal(cw_rec_t *rec, const struct timeval *timestamp, char *representation, bool *is_end_of_word, bool *is_error);
int  cw_rec_poll_character_internal(cw_rec_t *rec, const struct timeval *timestamp, char *c, bool *is_end_of_word, bool *is_error);
void cw_rec_clear_buffer_internal(cw_rec_t *rec);

/* Setters of receiver's essential parameters. */
int  cw_rec_set_speed_internal(cw_rec_t *rec, int new_value);
int  cw_rec_set_tolerance_internal(cw_rec_t *rec, int new_value);
int  cw_rec_set_gap_internal(cw_rec_t *rec, int new_value);
int  cw_rec_set_noise_spike_threshold_internal(cw_rec_t *rec, int new_value);
void cw_rec_set_adaptive_mode_internal(cw_rec_t *rec, bool adaptive);

/* Getters of receiver's essential parameters. */
float cw_rec_get_speed_internal(cw_rec_t *rec);
int   cw_rec_get_tolerance_internal(cw_rec_t *rec);
/* int   cw_rec_get_gap_internal(cw_rec_t *rec); */
int   cw_rec_get_noise_spike_threshold_internal(cw_rec_t *rec);
bool  cw_rec_get_adaptive_mode_internal(cw_rec_t *rec);

/* Receiver's reset functions. */
void cw_rec_reset_receive_parameters_internal(cw_rec_t *rec);
void cw_rec_reset_receive_statistics_internal(cw_rec_t *rec);
void cw_rec_reset_internal(cw_rec_t *rec);

/* Other helper functions. */
void cw_rec_sync_parameters_internal(cw_rec_t *rec);
void cw_rec_get_parameters_internal(cw_rec_t *rec,
				    int *dot_len_ideal, int *dash_len_ideal,
				    int *dot_len_min, int *dot_len_max,
				    int *dash_len_min, int *dash_len_max,
				    int *eom_len_min,
				    int *eom_len_max,
				    int *eom_len_ideal,
				    int *eoc_len_min,
				    int *eoc_len_max,
				    int *eoc_len_ideal,
				    int *adaptive_threshold);
void cw_rec_get_statistics_internal(cw_rec_t *rec, double *dot_sd, double *dash_sd,
				    double *element_end_sd, double *character_end_sd);
int cw_rec_get_buffer_length_internal(cw_rec_t *rec);
int cw_rec_get_receive_buffer_capacity_internal(void);





#ifdef LIBCW_UNIT_TESTS

unsigned int test_cw_rec_identify_mark_internal(void);
unsigned int test_cw_rec_with_base_data_fixed(void);
unsigned int test_cw_rec_with_random_data_fixed(void);
unsigned int test_cw_rec_with_random_data_adaptive(void);
unsigned int test_cw_get_receive_parameters(void);

#endif /* #ifdef LIBCW_UNIT_TESTS */





#endif /* #ifndef H_LIBCW_REC */
