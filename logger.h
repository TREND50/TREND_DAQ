#ifndef LOGGER_H
#define LOGGER_H


#define LOGGER_LONG_OPTIONS \
    {"verbosity", required_argument, 0,   'V'}

#define LOGGER_GETOPT_DESCRIPTOR "V:"


enum LoggerPriority {DEBUG=0, INFO=1, WARNING=2, ERROR=3};

int notify(int priority, char* msg, ...);

int logger_parse_option(char c, char* optarg);
char* logger_help_text();
char* logger_usage_text();

#endif
