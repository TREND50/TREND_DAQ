//======================================================================================
//
//  snapshot.c
//
//======================================================================================
//
//  
//
//======================================================================================
#include <syslog.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <getopt.h>
#include <stdio.h>

#include "daq_i.h"
#include "data_writer.h"
#include "logger.h"
#include "notifier.h"
#include "selector.h"


#define SPIKE_ALGO  slipps_find_spikes


//========================================================================================
//
//  Subroutines prototypes.
//
//========================================================================================

// Handle terminate signal for reliable data.
static int halt = 0; // stop flag control.
static void sig_int(int);

// Parse the input arguments.
int parse_inputs(int argsc, char** argsv, char** runid);

// Show help text on usage.
void print_usage(char* process);


//========================================================================================
int main( int argsc, char *argsv[] )
//========================================================================================
{
    // Parse the input arguments and set defaults.
    int   n_shoot     = 1;
    char* runid       = NULL;
    char* master_host = "u183";
            
    if (parse_inputs(argsc, argsv, &runid) < 0)
        exit(0);
    
    
    // Get the hostname.
    char host[8] = "u000";
    gethostname(host, sizeof(host));

    
    // Redirect the SIGINT interupt.
    signal(SIGINT, sig_int);


    // Initialize the DAQ.
    (*notifier_host()) = master_host;
    if (daq_start() < 0)
        return -1;

    int time[MAX_SPIKE];
    int n_time, master_rank;


    // Initialise data & log files.
    char datafile[] = "data.bin";
    char timefile[] = "time.bin";
    char logfile[]  = "log.txt";
    int irun  = atoi(runid);
    int ihost = atoi(host+1);

    dw_initialise(irun, ihost);
    dw_clear(datafile);
    dw_clear(timefile);
    dw_clear(logfile);


    // Processing loop.
    int iloop = 0;	
    while (halt == 0)
    {
        // Get the time at loop start.
        struct timeval tstart, tsync, tstop;
        gettimeofday(&tstart, NULL);

		
        // Synchronize with a buffer switch.
	if (daq_synchronise() < 0)
            break;
        int irq_start = daq_counter();


        // Check for interupt.
        if (halt != 0)
            break;


        // Map the iddle buffer.
        unsigned char* data = daq_data();


        // Find candidate spikes.
        gettimeofday(&tsync, NULL);
        SPIKE_ALGO(daq_buffer_size(), data, &n_time, time);

        // Log the loop status.
        int irq_stop = daq_counter();
        notify(INFO, "iloop = %d, irq = %d/%d", iloop, irq_start, irq_stop);

           
        // Write statistics to log file.
        gettimeofday(&tstop, NULL);
        double dtc = (tsync.tv_sec-tstart.tv_sec)+1.0e-6*(tsync.tv_usec-tstart.tv_usec);
        double dtw = (tstop.tv_sec-tsync.tv_sec)+1.0e-6*(tstop.tv_usec-tsync.tv_usec);
        double t0 = tstart.tv_sec+1.0e-6*tstart.tv_usec;

        dw_log(
            logfile,
            "%.3lf %.3lf %.3lf %d %d %d %d %d",
            t0, dtc, dtw, iloop, irq_start, irq_stop, n_time, 0
        );


        // Increment the loop index.
	iloop++;
	
	
	// Check for termination.
	if (iloop >= n_shoot)
	    break;
    }
	

    // Close the DAQ.
    daq_close();	


    return( 0 );
}


//========================================================================================
static void sig_int(int signo)
//========================================================================================
{
    notify(INFO, "Caught SIGINT, terminating program ...");
    halt = 1;

    return;
}


//========================================================================================
int parse_inputs(int argsc, char** argsv, char** runid)
//========================================================================================
//
//  Parse the inputs arguments.
//
//========================================================================================
{
    char c;

    // Initialisation of mandatory arguments.
    *selector_threshold() = 0.0;
    *runid                = NULL;

    // Parse the command line.
    while (1)
    {
        static struct option long_options[] =
        {
            {"help",          no_argument,       0, 'h'},
            {"runid",         required_argument, 0, 'r'},
            SELECTOR_LONG_OPTIONS,
            DAQ_LONG_OPTIONS,
	    DW_LONG_OPTIONS,
	    LOGGER_LONG_OPTIONS
        };

        int option_index = 0;
        c = getopt_long(argsc, argsv, 
	    "hr:" SELECTOR_GETOPT_DESCRIPTOR DAQ_GETOPT_DESCRIPTOR DW_GETOPT_DESCRIPTOR LOGGER_GETOPT_DESCRIPTOR,
	    long_options, &option_index
	);

        if (c == -1)
            break;
        else if (c == 'h')
        {
            print_usage(argsv[0]);
            return(-1);
        }
        else if (c == 'r')
            *runid = optarg;
        else
	{
           selector_parse_option(c, optarg);
           daq_parse_option(c, optarg);
	   dw_parse_option(c, optarg);
	   logger_parse_option(c, optarg);
	}
    }

    // Check if mandatory arguments where provided.
    if((*selector_threshold() == 0.0) || (*runid == NULL))
    {
        print_usage(argsv[0]);
        return(-1);
    }
    return(0);
}


//========================================================================================
void print_usage(char* proccess)
//========================================================================================
//
//  Show help text on usage.
//
//========================================================================================
{
    printf(
        "Usage: %s --runid=[int] %s %s %s %s\n"
        "* runid:           the runnumber for the data file name.\n",
        proccess, selector_usage_text(), daq_usage_text(), dw_usage_text(), logger_usage_text()
    );
    printf(selector_help_text());
    printf(daq_help_text());
    printf(dw_help_text());
    printf(logger_help_text());
}
