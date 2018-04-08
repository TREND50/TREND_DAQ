#ifndef DATA_WRITER_H
#define DATA_WRITER_H 1

#define dw_dump(filetag, n, data) dw_raw_dump(filetag, (n)*sizeof(*data), data)

#define DW_LONG_OPTIONS \
    {"dataloc", required_argument, 0,   'L'}

#define DW_GETOPT_DESCRIPTOR "L:"

char* dw_fullname(char* filetag);
char** dw_location();
int dw_initialise(int runid, int host);
int dw_clear(char* filetag);
int dw_log(char* filetag, char* line, ...);
int dw_raw_dump(char* filetag, int n, void* data);

int dw_parse_option(char c, char* optarg);
char* dw_help_text();
char* dw_usage_text();

#endif
