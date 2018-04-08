#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <mpi.h>
#include <math.h>
#include <string.h>
#include <getopt.h>
#include <stdio.h>

#include "daq_i.h"
#include "logger.h"
#include "data_writer.h"
#include "notifier.h"
#include "selector.h"


#define MPI_OK_TAG  1

#define SPIKE_ALGO  slipps_find_spikes
#define COINC_ALGO  slipps_find_coincidences


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
int main(int argsc, char **argsv)
//========================================================================================
{
    //====================================================================================
    // Initialise MPI.
    //====================================================================================
    MPI_Init(&argsc, &argsv);
    MPI_Errhandler_set(MPI_COMM_WORLD, MPI_ERRORS_RETURN);

    int mpi_rank;
    int mpi_n_process;

    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_n_process);

    notify(DEBUG, "MPI rank is %d / %d", mpi_rank, mpi_n_process);


    //====================================================================================
    // Parse the input arguments and set defaults.
    //====================================================================================
    char* runid       = NULL;
    char* master_host = "u183";
    
    if (parse_inputs(argsc, argsv, &runid) < 0)
        exit(0);
    
    
    // Get the hostname.
    char host[8] = "u000";
    gethostname(host, sizeof(host));

    
    // Redirect the SIGINT interupt.
    signal(SIGINT, sig_int);

    
    //====================================================================================
    //  Master process.
    //====================================================================================    
    if(strcmp(host, master_host) == 0)
    {
        int        time[MAX_ANTENNA][MAX_SPIKE];
        int        n_time[MAX_ANTENNA];
        char       decision[MAX_ANTENNA][MAX_SPIKE];
        MPI_Status mpi_status;


        // Notify other process that I am the master.
        int recv_rank;

        int rank_prev = mpi_rank-1;
        if (rank_prev == -1)
            rank_prev = mpi_n_process-1;
        int rank_next = mpi_rank+1;
        if (rank_next == mpi_n_process)
            rank_next = 0;

        MPI_Send(&mpi_rank, 1, MPI_INT, rank_next, MPI_OK_TAG, MPI_COMM_WORLD);
        MPI_Recv(&recv_rank, 1, MPI_INT, rank_prev, MPI_OK_TAG, MPI_COMM_WORLD, &mpi_status);

        if (recv_rank != mpi_rank)
        {
            notify(ERROR, "Broken chain when notifying master.");
            return -1;
        }


        // Map antenna ID's and initialise the selector.
        int antenna_id[MAX_ANTENNA];
        int ip, ia = 0;
        for(ip = 0; ip < mpi_n_process; ip++) if (ip != mpi_rank)
        {
            MPI_Recv(&antenna_id[ia], 1, MPI_INT, ip, MPI_OK_TAG, MPI_COMM_WORLD, &mpi_status);
            ia++;
        }
        selector_initialise(mpi_n_process-1, antenna_id);


        // Master loop.
        int iloop = 0;
        while (halt == 0)
	{
            // Synchronize with slaves process.
            MPI_Barrier(MPI_COMM_WORLD);


	    // Receive the spike times from all channels.
            ia = 0;
            for(ip = 0; ip < mpi_n_process; ip++) if (ip != mpi_rank)
            {
	        MPI_Recv(&time[ia][0], MAX_SPIKE, MPI_LONG, ip, MPI_OK_TAG, MPI_COMM_WORLD, &mpi_status);
                MPI_Get_count(&mpi_status, MPI_INT, &n_time[ia]);
                ia++;

		notify(DEBUG, "loop=%d, process=%d, spikes=%d", iloop, ip, n_time[ia]);
            }

            
            // Find candidate spikes.
            COINC_ALGO(mpi_n_process-1, n_time, time, decision);

	    
            // Send back the master decision to slaves.
            ia = 0;
            for(ip = 0; ip < mpi_n_process; ip++) if (ip != mpi_rank)
            {
	        MPI_Send(&decision[ia][0], n_time[ia], MPI_CHAR, ip, MPI_OK_TAG, MPI_COMM_WORLD);
                ia++;
            }

		
            iloop++;
	}
    }
	

    //====================================================================================
    //  Slave process.
    //====================================================================================        
    else 
    { 
        int time[MAX_SPIKE];
        int n_time, master_rank;
        char decision[MAX_SPIKE];
        MPI_Status mpi_status;
        unsigned char d_save[MAX_SPIKE*SAMPLE_SIZE];
        int t_save[MAX_SPIKE*TIME_SIZE];
        int n_save;

        // Circulate the information on the master.
        int rank_prev = mpi_rank-1;
        if (rank_prev == -1)
            rank_prev = mpi_n_process-1;
        int rank_next = mpi_rank+1;
        if (rank_next == mpi_n_process)
            rank_next = 0;

        MPI_Recv(&master_rank, 1, MPI_INT, rank_prev, MPI_OK_TAG, MPI_COMM_WORLD, &mpi_status);
        MPI_Send(&master_rank, 1, MPI_INT, rank_next, MPI_OK_TAG, MPI_COMM_WORLD);


	// Initialize the DAQ.
        (*notifier_host()) = master_host;
	if (daq_start() < 0)
            return -1;


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


        // Send the antenna id to the master.
        int antid = ihost-ANTENNA_ID_OFFSET;
        MPI_Send(&antid, 1, MPI_INT, master_rank, MPI_OK_TAG, MPI_COMM_WORLD);


        // Processing loop.
        int iloop = 0;
        n_save = 0;	
	while (halt == 0)
	{
            // Dump the previous data to file, if data integrity was OK.
            if (n_save > 0)
            {
                dw_dump(timefile, 4*n_save, t_save);
                dw_dump(datafile, SAMPLE_SIZE*n_save, d_save);
            }


            // Synchronize with slaves process.
            MPI_Barrier(MPI_COMM_WORLD);


            // Get the time at loop start.
            struct timeval tstart, tsync, tsend, trecv, tstop;
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
            float stddev = SPIKE_ALGO(daq_buffer_size(), data, &n_time, time);


            // Send the candidates spike times to the master.
            gettimeofday(&tsend, NULL);	
	    MPI_Send(time, n_time, MPI_INT, master_rank, MPI_OK_TAG, MPI_COMM_WORLD);

	        
            // Receive the master decision.
	    MPI_Recv(decision, n_time, MPI_CHAR, master_rank, MPI_OK_TAG, MPI_COMM_WORLD, &mpi_status);	
	    gettimeofday(&trecv, NULL);

            
            // Copy the selected spikes to memory.
            unsigned char* pd = d_save;
            n_save = 0;
            for (int it = 0; it < n_time; it++)
            {
                if (decision[it] == 0x1)
                {
                    // Append time data.
                    t_save[n_save*TIME_SIZE+0] = tstart.tv_sec;
                    t_save[n_save*TIME_SIZE+1] = irq_start;
                    t_save[n_save*TIME_SIZE+2] = time[it]/1024;
                    t_save[n_save*TIME_SIZE+3] = time[it]%1024;
                    n_save++;

                    // Copy the centered raw data.
                    int istart = time[it] - 512;
                    if (istart < 0)
                        istart = 0;
                    else if (istart >=  daq_buffer_size()-1024)
                        istart = daq_buffer_size()-1025;

                    memcpy(pd, data+istart, SAMPLE_SIZE);
                    pd += SAMPLE_SIZE;
                }
            }


            // Check data integrity.
            int irq_stop = daq_counter();
            if (irq_stop != irq_start)
                n_save = 0;


            // Log the loop status.
            notify(INFO, "iloop = %d, irq = %d/%d, trigger=%d/%d, sigma=%.1f", 
            iloop, irq_start, irq_stop, n_save, n_time, stddev);
            

            // Write statistics to log file.
            gettimeofday(&tstop, NULL);
            double dtc = (tsync.tv_sec-tstart.tv_sec)+1.0e-6*(tsync.tv_usec-tstart.tv_usec);
            double dta = (tsend.tv_sec-tsync.tv_sec)+1.0e-6*(tsend.tv_usec-tsync.tv_usec);
            double dtd = (trecv.tv_sec-tsend.tv_sec)+1.0e-6*(trecv.tv_usec-tsend.tv_usec);
            double dtw = (tstop.tv_sec-trecv.tv_sec)+1.0e-6*(tstop.tv_usec-trecv.tv_usec);
            double t0 = tstart.tv_sec+1.0e-6*tstart.tv_usec;

            dw_log(
                logfile,
                "%.3lf %.3lf %.3lf %.3lf %.3lf %d %d %d %d %d %.1f",
                t0, dtc, dta, dtd, dtw, iloop, irq_start, irq_stop, n_time, n_save, stddev
            );


            // Increment loop index.
	    iloop++;
        }
	

        // Close the DAQ.
        daq_close();	
    }


    // Close MPI.
    MPI_Finalize();

    return 0;
}


//================================================================
static void sig_int(int signo)
//================================================================
{
    notify(INFO, "Caught SIGINT, terminating program ...");
    halt = 1;

    return;
}


//================================================================
int parse_inputs(int argsc, char** argsv, char** runid)
//================================================================
//
//  Parse the inputs arguments.
//
//================================================================
{
    char c;

    // Initialisation of mandatory arguments.
    *selector_threshold()    = 0.0;
    *runid                   = NULL;
    *selector_multiplicity() = 0;

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
    if((*selector_threshold() == 0.0) || (*runid == NULL) || (*selector_multiplicity() == 0))
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
        "Usage: %s --runid=[int] %s %s %s %s\n"
        "* runid:           the runnumber for the data file name.\n",
        proccess, selector_usage_text(), daq_usage_text(), dw_usage_text(), logger_usage_text()
    );
    printf(selector_help_text());
    printf(daq_help_text());
    printf(dw_help_text());
    printf(logger_help_text());
}
