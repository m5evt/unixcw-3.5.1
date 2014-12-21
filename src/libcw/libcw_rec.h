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





int  cw_rec_set_gap_internal(cw_rec_t *rec, int new_value);
void cw_rec_reset_receive_parameters_internal(cw_rec_t *rec);
void cw_rec_sync_parameters_internal(cw_rec_t *rec);





#ifdef LIBCW_UNIT_TESTS

unsigned int test_cw_rec_identify_mark_internal(void);
unsigned int test_cw_rec_with_base_data_fixed(void);
unsigned int test_cw_rec_with_random_data_fixed(void);
unsigned int test_cw_rec_with_random_data_adaptive(void);
unsigned int test_cw_get_receive_parameters(void);

#endif /* #ifdef LIBCW_UNIT_TESTS */





#endif /* #ifndef H_LIBCW_REC */
