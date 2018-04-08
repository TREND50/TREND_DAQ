#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "logger.h"

struct {
    int  verbosity;
    char hostname[8];
} logger_ctl = {
    INFO,
    "0000"
}; 


int notify(int priority, char* msg, ...)
{
    if (priority < logger_ctl.verbosity)
        return(0); 

    char* strprio[4] = {
        "DEBUG",
        "INFO",
        "WARNING",
        "ERROR"
    }; 

    // Get hostname if required.
    if (logger_ctl.hostname[0] == '0')
        gethostname(logger_ctl.hostname, sizeof(logger_ctl.hostname));

    // Format message.
    printf("[%4s] %-8s ", logger_ctl.hostname, strprio[priority]);

    va_list args;
    va_start(args, msg);
    vprintf(msg, args);
    va_end(args);

    printf("\n");

    return 0;
}

int logger_parse_option(char c, char* optarg)
{
    if (c == 'V')
    {
        if (strlen(optarg) > 0)
	{
	   if (strcmp(optarg, "DEBUG") == 0)
               logger_ctl.verbosity = DEBUG;
	   else if (strcmp(optarg, "INFO") == 0)
	       logger_ctl.verbosity = INFO;
	   else if (strcmp(optarg, "WARNING") == 0)
	       logger_ctl.verbosity = WARNING;
	   else if (strcmp(optarg, "ERROR") == 0)
	       logger_ctl.verbosity = ERROR;
	   else
	       notify(ERROR, "Unknown verbosity %s", optarg);
        }
    }

    return 0;
}


char loggerhelp[] =
    "* verbosity:       the logger verbosity level in {DEBUG, INFO, WARNING, ERROR}.\n"; 

char* logger_help_text()
{
    return loggerhelp;
}


char loggerusage[] = "(--verbosity=[char*])";

char* logger_usage_text()
{
    return loggerusage;
}
