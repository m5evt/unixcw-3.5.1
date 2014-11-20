/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_REC
#define H_LIBCW_REC





/* Dot length magic number; from PARIS calibration, 1 Dot=1200000/WPM
   usec.

   This variable is used in generator code as well. */
enum { CW_DOT_CALIBRATION = 1200000 };


/* Receiver contains a fixed-length buffer for representation of received data.
   Capacity of the buffer is vastly longer than any practical representation.
   Don't know why, a legacy thing. */
enum { CW_REC_REPRESENTATION_CAPACITY = 256 };





/* Forward declaration of a type. */
struct cw_rec_struct;
typedef struct cw_rec_struct cw_rec_t;




int  cw_rec_set_gap_internal(cw_rec_t *rec, int new_value);
void cw_rec_reset_receive_parameters_internal(cw_rec_t *rec);
void cw_rec_sync_parameters_internal(cw_rec_t *rec);





#ifdef LIBCW_UNIT_TESTS

unsigned int test_cw_rec_identify_mark_internal(void);

#endif /* #ifdef LIBCW_UNIT_TESTS */





#endif /* #ifndef H_LIBCW_REC */
