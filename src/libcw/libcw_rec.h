/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License, version 2 or later.
*/

#ifndef H_LIBCW_REC
#define H_LIBCW_REC




#include "libcw2.h"




/* Dot length magic number.

   From PARIS calibration, 1 dot length [us] = 1200000 / speed [wpm].

   This variable is used in generator code as well. */
enum { CW_DOT_CALIBRATION = 1200000 };




void cw_rec_reset_parameters_internal(cw_rec_t * rec);
void cw_rec_sync_parameters_internal(cw_rec_t * rec);
void cw_rec_get_parameters_internal(cw_rec_t * rec,
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

#include "libcw_test.h"

unsigned int test_cw_rec_identify_mark_internal(cw_test_stats_t * stats);
unsigned int test_cw_rec_test_with_base_constant(cw_test_stats_t * stats);
unsigned int test_cw_rec_test_with_random_constant(cw_test_stats_t * stats);
unsigned int test_cw_rec_test_with_random_varying(cw_test_stats_t * stats);
unsigned int test_cw_rec_get_parameters(cw_test_stats_t * stats);
unsigned int test_cw_rec_parameter_getters_setters_1(cw_test_stats_t * stats);
unsigned int test_cw_rec_parameter_getters_setters_2(cw_test_stats_t * stats);

#endif /* #ifdef LIBCW_UNIT_TESTS */




#endif /* #ifndef H_LIBCW_REC */
