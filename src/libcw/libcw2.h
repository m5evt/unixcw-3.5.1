/*
  This file is part of unixcw project.

  unixcw project is distributed under the terms of GNU GPL 2+ license.
*/

#ifndef H_LIBCW2
#define H_LIBCW2





struct cw_gen_struct;
typedef struct cw_gen_struct cw_gen_t;





int cw_gen_set_frequency_internal(cw_gen_t *gen, int new_value);
int cw_gen_set_volume_internal(cw_gen_t *gen, int new_value);
int cw_gen_set_speed_internal(cw_gen_t *gen, int new_value);
int cw_gen_set_gap_internal(cw_gen_t *gen, int new_value);
int cw_gen_set_weighting_internal(cw_gen_t *gen, int new_value);





#endif /* #ifndef H_LIBCW2 */
