/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/


#ifndef H_CW_CONSOLE
#define H_CW_CONSOLE



//#include "libcw.h"
#include "libcw_internal.h"



int  cw_console_configure(cw_gen_t *gen, const char *device);
int  cw_console_write(cw_gen_t *gen, cw_tone_t *tone);
void cw_console_silence(cw_gen_t *gen);



#endif //#ifndef H_CW_CONSOLE
