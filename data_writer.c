#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "data_writer.h"
#include "logger.h"


#define DW_COMMAND_OFFSET 6


struct {
    char* location;
    char  run[8];
    char  host[8];
    char  command[256];
} dw_ctl = {
    "/data/current",
    "R000000",
    "A0000",
    "rm -f "
};


char* dw_fullname(char* filetag)
{
    sprintf(dw_ctl.command+DW_COMMAND_OFFSET, "%s/%s/%s_%s_%s",
    dw_ctl.location, dw_ctl.run, dw_ctl.run, dw_ctl.host, filetag);

    return(dw_ctl.command+DW_COMMAND_OFFSET);
}


char** dw_location()
{
    return &dw_ctl.location;
}


int dw_initialise(int runid, int host)
{
    // Set tags.
    sprintf(dw_ctl.run, "R%06d", runid);
    sprintf(dw_ctl.host, "A%04d", host);


    // Make run directory.
    char buffer[256];
    sprintf(buffer, "mkdir -p %s/%s", dw_ctl.location,  dw_ctl.run);
    system(buffer);

    return(0);
}


int dw_clear(char* filetag)
{
    dw_fullname(filetag); 
    system(dw_ctl.command);

    return(0);
}


int dw_log(char* filetag, char* line, ...)
{
    char* file = dw_fullname(filetag);

    FILE* fid = fopen(file, "ab+");
    if (fid == NULL)
    {
        notify(ERROR, "Couldn't open file %s", file);
        return(-1);
    }

    va_list args;
    va_start(args, line);
    vfprintf(fid, line, args);
    va_end(args);

    fputs("\n", fid);
    fclose(fid);

    return 0;
}


int dw_raw_dump(char* filetag, int n, void* data)
{
    // Check for null data.
    if (n <= 0)
        return(0);


    // Append the data to file.
    char* file = dw_fullname(filetag);

    FILE* fid = fopen(file, "ab+");
    if (fid == NULL)
    {
        notify(ERROR, "Couldn't open file %s", file);
        return(-1);
    }

    int nwt = fwrite(data, sizeof(char), n, fid);
    fclose(fid);    

    if (nwt != n)
    {
        notify(WARNING, "Incomplete dump to file %s (%d / %d).", file, nwt, n);
        return(-1);
    }
  
    return(0);
}


int dw_parse_option(char c, char* optarg)
{
    if (c == 'L')
    {
        if (strlen(optarg) > 0)
           dw_ctl.location = optarg;
    }

    return 0;
}


char dwhelp[] = 
    "* dataloc:         the location where to store the data.\n";

char* dw_help_text()
{
    return dwhelp;
}


char dwusage[] = "(--dataloc=[char*])";

char* dw_usage_text()
{
    return dwusage;
}

