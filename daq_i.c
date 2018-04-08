#include <getopt.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "daq_i.h"
#include "apex_tools.h"
#include "simdaq.h"
#include "logger.h"

struct {
    int type;
    int mode;
    char* simopts;
}
daq_ctl = {
    DEFAULT_DAQ_TYPE,
    DEFAULT_DAQ_MODE,
    "/data/simdaq"
};
 

struct {
    int fd;
    int irq; 
    int offset;
}
apex_ctl = {
    0,
    0,
    0
};


int daq_start()
{
    if (daq_ctl.mode == Slave)
    {
        // Acces to the DAQ in slave mode.
        //===
        int ret = daq_join();
        if (ret < 0)
        {
            notify(ERROR, "Failed to initialise the DAQ!");
            return ret;
        }
    }
    else
    {
        // Initialize the DAQ in master mode.
        //===
        int ret = daq_init();
        if (ret < 0)
        {
            notify(ERROR, "Failed to initialise the DAQ!");
            return ret;
        }
    }

    return 0;
}


int daq_join()
{
   if (daq_ctl.type == Apex)
        return joinApex(&apex_ctl.fd);
   else if (daq_ctl.type == Sim)
        return simdaq_init(daq_ctl.simopts);
   else
        return -1; 
}


int daq_init()
{
    if (daq_ctl.type == Apex)
        return initApex(&apex_ctl.fd);
    else if (daq_ctl.type == Sim)
        return simdaq_init(daq_ctl.simopts);
    else
        return -1;
}


int daq_synchronise()
{
    if (daq_ctl.type == Apex)
        return synchroniseWithApex(&apex_ctl.fd);
    else if (daq_ctl.type == Sim)
        return simdaq_synchronise();
    else
        return -1;
}


int daq_counter()
{
    if (daq_ctl.type == Apex)
        return getApexIRQ(&apex_ctl.fd);
    else if (daq_ctl.type == Sim)
        return simdaq_counter();
    else
        return -1;
}


unsigned char* daq_data()
{
    if (daq_ctl.type == Apex)
        return iddleApexBuffer();
    else if (daq_ctl.type == Sim)
        return simdaq_data();
    else
        return NULL;
}


int daq_copy_data(unsigned char* data, int length)
{
    if (daq_ctl.type == Apex)
        return getApexRawData(data, length, &apex_ctl.irq, &apex_ctl.offset, &apex_ctl.fd);
    else if (daq_ctl.type == Sim)
        return simdaq_copy_data(data, length);
    else
        return -1;
}


int daq_close()
{
    if (daq_ctl.type == Apex)
    {
        if (daq_ctl.mode == Master)
            return closeApex(&apex_ctl.fd);
        else
        {
            close(apex_ctl.fd);
            return 0;
        }
    }
    else if (daq_ctl.type == Sim)
        return simdaq_close();
    else
        return -1;
}


int daq_irq()
{
    if (daq_ctl.type == Apex)
        return apex_ctl.irq;
    else if (daq_ctl.type == Sim)
        return simdaq_irq();
    else
        return 0;
}


int daq_offset()
{
    if (daq_ctl.type == Apex)
        return apex_ctl.offset;
    else if (daq_ctl.type == Sim)
        return simdaq_offset();
    else
        return 0;
}


int* daq_type()
{
    return(&daq_ctl.type);
}


int* daq_mode()
{
    return(&daq_ctl.mode);
}


char** daq_simopts()
{
    return(&daq_ctl.simopts);
}


int daq_buffer_size()
{
    if (daq_ctl.type == Apex)
        return DMA_SIZE;
    else if (daq_ctl.type == Sim)
        return SIMDAQ_SIZE;
    else
        return 0;
}


int daq_parse_option(char c, char* optarg)
{
    if (c == 'M')
    {
        if (strlen(optarg) > 0)
        {
            if (strcmp(optarg, "Master") == 0)
                daq_ctl.mode = Master;
            else if (strcmp(optarg, "Slave") == 0)
                daq_ctl.mode = Slave;
            else
                notify(ERROR, "Unknown daq mode %s", optarg);
        }
    }
    else if (c == 'D')
    {
        if (strlen(optarg) > 0)
        {
            if (strcmp(optarg, "Apex") == 0)
                daq_ctl.type = Apex;
            else if (strcmp(optarg, "Sim") == 0)
                daq_ctl.type = Sim;
            else
                notify(ERROR, "Unknown daq type %s", optarg);
        }
    }
    else if (c == 'O')
    {
        if (strlen(optarg) > 0)
           daq_ctl.simopts = optarg;
    }

    return 0;
}


char daqhelp[] = 
    "* daqmode:         'Master' or 'Slave' mode for the daq.\n"
    "* daqtype:         'Apex' or 'Sim' for running in harware or emulated mode.\n"
    "* simopts:         the folder from where to take simulated data.\n";

char* daq_help_text()
{
    return daqhelp;
}


char daqusage[] = "(--daqmode=[char*]) (--daqtype=[char*]) (--simopts=[char*])";

char* daq_usage_text()
{
    return daqusage;
}

