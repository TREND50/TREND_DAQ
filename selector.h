#ifndef SELECTOR_H
#define SELECTOR_H 1

#define MAX_ANTENNA 80
#define MAX_SPIKE   256
#define SAMPLE_SIZE 1024
#define TIME_SIZE   4


#define ANTENNA_ID_OFFSET       101
#define POST_SPIKE_DEAD_TIME    32
#define SELECTOR_T_WINDOW       1.2


#define CONSTANT_C0 3.0e+8
#define CONSTANT_TS 5.0e-9


#define SELECTOR_LONG_OPTIONS \
    {"threshold",    required_argument, 0, 't'},\
    {"multiplicity", required_argument, 0, 'm'},\
    {"detconfig",    required_argument, 0, 'C'}

#define SELECTOR_GETOPT_DESCRIPTOR "t:m:C:"


float* selector_threshold();
int* selector_multiplicity();
char** selector_config();

int selector_initialise(int n_antenna, int antenna_id[MAX_ANTENNA]);

float slipps_find_spikes(int n_data, unsigned char* data, int* n_time, int time[MAX_SPIKE]);
int slipps_find_coincidences(int n_antenna, int n_time[MAX_ANTENNA], int time[MAX_ANTENNA][MAX_SPIKE], char 
decision[MAX_ANTENNA][MAX_SPIKE]);
float selector_find_spikes(int n_data, unsigned char* data, int* n_time, int time[MAX_SPIKE]);
int selector_find_coincidences(int n_antenna, int n_time[MAX_ANTENNA], int time[MAX_ANTENNA][MAX_SPIKE], char 
decision[MAX_ANTENNA][MAX_SPIKE]);

int selector_parse_option(char c, char* optarg);
char* selector_help_text();
char* selector_usage_text();

#endif
