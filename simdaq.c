#include "simdaq.h"
#include "logger.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>


struct {
    FILE*            fid;
    int              irq;
    int              offset;
    int              pos;
    unsigned char    buffer[SIMDAQ_SIZE];
}
simdaq_ctl = {
    NULL,
    1,
    0,
    0
};


int simdaq_init(char* data_dir)
{
    char hostname[8] = "u000";
    gethostname(hostname, strlen(hostname));

    char buffer[256];
    sprintf(buffer, "%s/%s.sim", data_dir, hostname); 

    simdaq_ctl.fid = fopen(buffer, "r");
    if (simdaq_ctl.fid == NULL)
    {
        notify(ERROR, "In simdaq_init: couldn't access data file %s", buffer);
        return -1;
    }

    simdaq_ctl.irq    = 1;
    simdaq_ctl.offset = 0;
    simdaq_ctl.pos    = 0;

    return 0;
}


int simdaq_synchronise()
{
    unsigned char* data = simdaq_ctl.buffer;
    int nread = 0;
    while (nread < SIMDAQ_SIZE)
    {
        int n = fread(data, sizeof(unsigned char), SIMDAQ_SIZE-nread, simdaq_ctl.fid);
        if (feof(simdaq_ctl.fid))
            rewind(simdaq_ctl.fid);
	
	data  += n;
	nread += n;
    }

    simdaq_ctl.irq += 1;

    return 0;
}


int simdaq_counter()
{
    return simdaq_ctl.irq-1;
}


unsigned char* simdaq_data()
{
    return simdaq_ctl.buffer;
}


int simdaq_copy_data(unsigned char* data, int length)
{
    int nread = 0;
    while (nread < length)
    {
        nread += fread(data, sizeof(unsigned char), length-nread, simdaq_ctl.fid);
        if (feof(simdaq_ctl.fid))
            rewind(simdaq_ctl.fid);
    }

    simdaq_ctl.offset  = simdaq_ctl.pos; 
    simdaq_ctl.pos    += nread;
    if (simdaq_ctl.pos >= DMA_SIZE)
    {
        simdaq_ctl.irq    += simdaq_ctl.pos/DMA_SIZE;
        simdaq_ctl.pos     = (simdaq_ctl.pos % DMA_SIZE);
    }

    return 0;
}


int simdaq_close()
{
    if (simdaq_ctl.fid != NULL)
        fclose(simdaq_ctl.fid);

    return(0);
}


int simdaq_irq()
{
    return simdaq_ctl.irq;
}


int simdaq_offset()
{
    return simdaq_ctl.offset;
}
