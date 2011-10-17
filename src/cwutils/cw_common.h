#ifndef _CW_COMMON_H
#define _CW_COMMON_H

#if defined(__cplusplus)
extern "C" {
#endif

#define CW_PRACTICE_TIME_MIN        1
#define CW_PRACTICE_TIME_MAX       99
#define CW_PRACTICE_TIME_INITIAL   15
#define CW_PRACTICE_TIME_STEP       1


typedef struct {
	int audio_system;
	char *audio_device;
	int send_speed;
	int frequency;
	int volume;
	int gap;
	int weighting;
	int practice_time;
	char *input_file;
	char *output_file;

	int is_cw;
	int has_practice_time;
	int has_outfile;

	/*
	 * Program-specific state variables, settable from the command line, or from
	 * embedded input stream commands.  These options may be set by the embedded
	 * command parser to values other than strictly TRUE or FALSE; all non-zero
	 * values are equivalent to TRUE.
	 *
	 * These fields are used only in cw.
	 */
	int do_echo;           /* Echo characters */
        int do_errors;         /* Print error messages to stderr */
        int do_commands;       /* Execute embedded commands */
        int do_combinations;   /* Execute [...] combinations */
        int do_comments;       /* Allow {...} as comments */
} cw_config_t;



extern void cw_print_help(const char *program_name, cw_config_t *config);
extern cw_config_t *cw_config_new(void);
extern void cw_config_delete(cw_config_t **config);
extern int cw_config_is_valid(cw_config_t *config);
extern int cw_generator_new_from_config(cw_config_t *config, const char *argv0);
extern void cw_start_beep(void);
extern void cw_end_beep(void);



#if defined(__cplusplus)
}
#endif


#endif /* _CW_COMMON_H */

