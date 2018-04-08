/**
 * background.c
 * record the antenna data every 5 min, for sensitive antenna
 * the average noise should be the milkway radiation, and it should vary in a distinguished  pattern 
 */
#include <syslog.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <getopt.h>
#include <string.h>
#include <stdio.h>

#include "ipps.h"
#include "daq_i.h"
#include "data_writer.h"
#include "logger.h"

#define data_length (1024) //the data size for each record 
#define DataSampleLength data_length


// Handle terminate signal for reliable data.
static void sig_int(int);

// Parse the input arguments.
int parse_inputs(int argsc, char** argsv, int* length,
int* period, char** runid);

// Show help text on usage.
void print_usage(char* process);


int halt=0; //main loop stop flag

int main(int argsc, char **argsv)
{
	int ret; //return value
	unsigned char *pdata; //buffer to hold each record
	int irq_count=0; //use this to track which data buffer we are at
	int last_irq_count=0;
	char timefilename[] = "BACK_time.bin";
	char datafilename[] = "BACK_data.bin";
	char hostname[65];
	int parameters[4];
	int time_interval = (5); //time interval between each data sample
	char *runnumber;//date file name  sequence
	int N;//the data_size (multiples of data_length KB) 
	int daq_mode = 0;

        // Parse the input arguments.
        if (parse_inputs(argsc, argsv, &N, &time_interval, &runnumber) < 0)
            return 0;

	pdata = malloc(data_length*N);
	Ipp32f *pDataSample=ippsMalloc_32f(data_length*N);
	if ( ( pDataSample == NULL ) || ( pdata == NULL )  )
	{
	  notify(ERROR, "Couldn't allocate enough memory. Aborting" );
          return 0;
	}

	Ipp32f DataSampleMean;
	Ipp32f DataSampleStdev;
	Ipp32f DataSampleMax;
	int DataSampleMaxIndex;

        // Initialise the data writer.
	gethostname(hostname,sizeof(hostname));
	int irun  = atoi(runnumber);
	int ihost = atoi(hostname+1);

        dw_initialise(irun, ihost);
        dw_clear(datafilename);
        dw_clear(timefilename);

        // Start the DAQ.
	if (daq_start() < 0)
            return 0;
        
	signal(SIGINT, sig_int);

	int t=0;
	int last_time=0;
	int count = 0; //loop counter
	while (halt==0)
	{
		t=time(NULL);
		if ((t-last_time)>time_interval) {

			last_time=t;
                       
			//read data from dma buffer
                        daq_copy_data(pdata, data_length*N);

			//do some data processing 
			int i=0;
                        for(i=0;i<data_length*N;i++){
                                *(pDataSample+i)=(Ipp32f)pdata[i];
                        }
			ippsStdDev_32f(pDataSample,DataSampleLength,&DataSampleStdev,ippAlgHintFast);
                        ippsMaxIndx_32f(pDataSample, DataSampleLength, &DataSampleMax, &DataSampleMaxIndex);
                        ippsMean_32f(pDataSample,DataSampleLength,&DataSampleMean,ippAlgHintFast);

			// Log to screen and file
			parameters[0]=t;
			parameters[1]=(int)(10.0*DataSampleStdev);
			parameters[2]=(int)DataSampleMean;
			parameters[3]=(int)DataSampleMax;
			
                        dw_dump(datafilename, N*data_length, pdata);
                        dw_dump(timefilename, 4, parameters);

			notify(INFO, "loop=%d, t=%d, 10*std=%3d, mean=%3d, max=%3d", 
                        count, parameters[0], parameters[1], parameters[2], parameters[3]);
			count++;
		}
                else {
			sleep(1);
		}

	}
	free( pdata );
	ippsFree( pDataSample );
	
        daq_close();

	return 0;
}

static void sig_int(int signo)
{
	notify(WARNING, "Caught SIGINT, terminating program...");
	halt = 1;
	return;
}


//================================================================
int parse_inputs(int argsc, char** argsv, int* length,
int* period, char** runid)
//================================================================
//
//  Parse the inputs arguments.
//
//================================================================
{
    char c;

    // Initialisation of mandatory arguments.
    *length = 0;
    *period = 0;
    *runid  = NULL;

    // Parse the command line.
    while (1)
    {
        static struct option long_options[] =
        {
            {"help",    no_argument,       0, 'h'},
            {"length",  required_argument, 0, 'l'},
            {"period",  required_argument, 0, 'p'},
            {"runid",   required_argument, 0, 'r'},
            DAQ_LONG_OPTIONS,
            DW_LONG_OPTIONS,
            LOGGER_LONG_OPTIONS
        };

        int option_index = 0;
        c = getopt_long(argsc, argsv, 
	    "hl:p:r:" DAQ_GETOPT_DESCRIPTOR DW_GETOPT_DESCRIPTOR LOGGER_GETOPT_DESCRIPTOR, 
	    long_options, &option_index
	);

        if (c == -1)
            break;
        else if (c == 'h')
        {
            print_usage(argsv[0]);
            return(-1);
        }
        else if (c == 'l')
            *length = strtod(optarg, NULL);
        else if (c == 'p')
            *period = strtod(optarg, NULL);
        else if (c == 'r')
            *runid = optarg;
        else
        {
            daq_parse_option(c, optarg);
            dw_parse_option(c, optarg);
            logger_parse_option(c, optarg);
        }
    }


    // Check if mandatory arguments where provided.
    if((*length == 0) || (*period == 0) || (*runid == NULL))
    {
        print_usage(argsv[0]);
        return(-1);
    }
    return(0);
}


//================================================================
void print_usage(char* proccess)
//================================================================
//
//  Show help text on usage.
//
//================================================================
{
    printf(
        "Usage: %s --length=[int] --period=[int] --runid=[int] %s %s %s\n"
        "* length:          the data length on wich to measure the background, in kB.\n"
        "* period:          the period of repetition of the background measurements.\n"
        "* runid:           the runnumber for the data file name.\n",
        proccess, daq_usage_text(), dw_usage_text(), logger_usage_text()
    );
    printf(daq_help_text());
    printf(dw_help_text());
    printf(logger_help_text());
}

