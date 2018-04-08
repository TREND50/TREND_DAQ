//
// cosmic-ray-mpi.c
// 
// Author: Meng Zhao (mengzhao@yahoo.com)
//
// Created: 2010-03-28
// Last Updated: 2010-03-28
//
// Purpose: The cosmic-ray program picks out spike signals from antanna/scintilator data streams whoes max is above a given threshold.
// 	Each signal channel's spike times are sent to the server for coincident filtering, 
// 	the resulting candidate time slices are send back to each channel such that coincident candidate data are saved to file
// 
// Performance Requirement: limit network data flow, 100% data stream processing efficency
// 
// Technique: MPI multi-processes parallel data processing
//
#include <syslog.h>
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

#include "ipps.h"
#include "daq_i.h"
#include "logger.h"
#include "data_writer.h"
#include "notifier.h"

#define work_data_length (128*1024*1024)
#define spike_data_length (1024)
#define MAX_CHANNEL_COUNT  (80)
#define spike_count_max  (16*1024)
#define DataSampleLength spike_data_length
#define DataSampleLargeLength (4*spike_data_length)
#define time_slice_size (4*1024)
#define trigger_threhold  (100)


// Handle terminate signal for reliable data.
static void sig_int(int);

// Parse the input arguments.
int parse_inputs(int argsc, char** argsv, float* threshold, 
char** runid, int* multiplicity);

// Show help text on usage.
void print_usage(char* process);


int halt=0; //main loop stop flag

int main(int argsc, char **argsv)
{
	float N=trigger_threhold; //trigger threhold =N*stddev
	char *runnumber;//date file name  sequence
	int fd, ret;
	int count;
	int channel_count = MAX_CHANNEL_COUNT;
	int coincident_count_threshold;
	
        static unsigned char work_data[(long)work_data_length]; //chunck of data read from DMA buffer
	static unsigned char spike_data[spike_count_max*spike_data_length]; //chunck of data with spike
	static unsigned char spike_data_save[spike_count_max*spike_data_length]; //chunck of data with spike
	static int  spike_info[spike_count_max*4]; //spike data time info
	static int  spike_info_save[spike_count_max*4]; //spike data time info
	static long spike_time[spike_count_max]; //spike position relative to the start of work_data

	memset(spike_data,0,spike_count_max*spike_data_length*sizeof(unsigned char));
	memset(spike_data_save,0,spike_count_max*spike_data_length*sizeof(unsigned char));
	memset(spike_info,0,spike_count_max*4*sizeof(int));
	memset(spike_info_save,0,spike_count_max*4*sizeof(int));
	memset(spike_time,0,spike_count_max*sizeof(long));
	
        int irq_count=0;
	int last_irq_count=0;
	int  buffer_offset=0;
	char timefilename[] = "time.bin";
	char datafilename[] = "data.bin";
	char logfilename[]  = "log.txt";
	char line[128];
 
        char hostname[8] = "u000";
        gethostname(hostname,sizeof(hostname));

        // Parse the input arguments.
        if (parse_inputs(argsc, argsv, &N, &runnumber, &coincident_count_threshold) < 0)
            exit(0);
        
	Ipp32f *pDataSample=ippsMalloc_32f(spike_data_length);
	Ipp32f *pDataSampleLarge=ippsMalloc_32f(DataSampleLargeLength);
	Ipp32f DataSampleMean;
	Ipp32f DataSampleStdev;
	Ipp32f DataSampleMaxStdev;
	Ipp32f DataSampleMax;
	int DataSampleMaxIndex;
        int irun=atoi(runnumber);
	int ihost=atoi(hostname+1);
	
        // Initialise the data writer.
        dw_initialise(irun, ihost);

        dw_clear(datafilename);
        dw_clear(timefilename);
        dw_clear(logfilename);

        // Redirect SIGINT interupt.
	signal(SIGINT,sig_int);

	//mpi stuff
	int myMPIRank;//mpi process id
        int myMPITag;//current process work type(0=server,1=send,2=receive)
        int mpi_process_count;//process size
        int node_name_length;
        char node_name[MPI_MAX_PROCESSOR_NAME]; //mpi process hostname

        MPI_Init(&argsc,&argsv);  //MPI initiation
        MPI_Comm_rank(MPI_COMM_WORLD,&myMPIRank); //Get the current process id
        MPI_Comm_size(MPI_COMM_WORLD,&mpi_process_count);//Get the number of process
        MPI_Get_processor_name(node_name,&node_name_length);//Get the processor's name
        MPI_Errhandler_set(MPI_COMM_WORLD, MPI_ERRORS_RETURN);

        notify(DEBUG, "MPI rank is %d / %d", myMPIRank, mpi_process_count);

        MPI_Status *pMPISendStatus,*pMPIReceiveStatus;//array to hold send/receive status
        MPI_Request *pMPISendRequests,*pMPIReceiveRequest;//array 

	int i,n;
        channel_count = mpi_process_count-1;
	int server_mpi_rank=channel_count;
	unsigned long spike_times[channel_count][spike_count_max];
	int channel_mpi_ranks[channel_count];
	MPI_Status mpi_receive_statuses[channel_count];
	int spike_counts[channel_count];
	long time_lower=0, time_upper=0, time_middle=0; 
	int time_slice_count=0;
	for(i=0; i<channel_count; i++){
		channel_mpi_ranks[i]=i;
	}


        int loop_count=0;
        //////////////////////////////////////////////////
        // Server process
        /////////////////////////////////////////////////
        if(myMPIRank == server_mpi_rank){
	while (halt==0)
	{
		if (loop_count >=999999)
		break;

		int coincident_count=0;	

		//receive spike_times from all channel
		for(n=0; n<channel_count; n++){
			MPI_Recv(&spike_times[n][0], spike_count_max,
					MPI_LONG,channel_mpi_ranks[n],1,
					MPI_COMM_WORLD, &mpi_receive_statuses[n]);
			MPI_Get_count(&mpi_receive_statuses[n],MPI_LONG,&spike_counts[n]);
			notify(DEBUG, "loop=%d, n=%d, spike_count=%d", loop_count, n, spike_counts[n]);
		}

		//coincident filter
		time_lower=0;
		time_middle=time_lower + time_slice_size/2;
		time_upper=time_middle + time_slice_size/2;
		time_slice_count=0;
		int last_spike_indexes[channel_count]; //hold the spike postion last proessed
		for(n=0; n<channel_count; n++){ //n is the channel index
			last_spike_indexes[n]=0;
		}
		while(time_upper< (long)work_data_length){
			//get the coincident_count for this time slice:
			//for each channel, determine if there is any spike within the time slice
			coincident_count=0;
			for(n=0; n<channel_count; n++){ //n is the channel index
				//determin current spike_indexes for each channel and coincident_count
				for(i=last_spike_indexes[n];i<spike_counts[n];i++){
					if ( spike_times[n][i] < time_lower) {
						//if the spike is not within the time slice, move to next spike
						continue;
					}else if (spike_times[n][i] > time_upper) {
						break;
					}else {
						//if the spike is within the time slice, add one coincident_count for this channel
						//then move to the next channel
					        notify(DEBUG, "time_slice_count=%d, spike_times[%d][%d]=%ld, time_lower=%ld",
                                                time_slice_count, n, i, spike_times[n][i], time_lower);
						coincident_count++;
						break;
					}
				}
				last_spike_indexes[n] = i;
			}  	
				
			if(coincident_count>=coincident_count_threshold){
				//this slice is "good" mark all the spikes in the slice (set spike_time=-1)
#if 1
				//mark spikes that is within the time slice  
				for(n=0; n<channel_count; n++){ //n is the channel index
					for(i=last_spike_indexes[n];i<spike_counts[n];i++){ //start from the last spike position
						if (spike_times[n][i] < time_lower) {
							//this never happens 
							continue;
						}else if (spike_times[n][i] > time_upper) {
							break;
						}else { //spike is inside the time slice
							//mark spike
							spike_times[n][i]=-1;
							continue;
						}
					}
					//remember the last spike position
					last_spike_indexes[n]=i;
				}	
#endif

			}

			//stepping 1/2 of the slice to ensure proper coverage
			time_lower=time_middle;
			time_middle=time_upper + time_slice_size/2;
			time_upper=time_middle+time_slice_size/2;
			time_slice_count++;
		}//end coincident filter

		//send filtered spike_time to node,
		for(n=0; n<channel_count; n++){ //n is the channel index
			MPI_Send(&spike_times[n][0],spike_counts[n],MPI_LONG,channel_mpi_ranks[n],1,MPI_COMM_WORLD);
		} 

		loop_count++;
		MPI_Barrier(MPI_COMM_WORLD);
		
	}
	}//end server process
	
	/////////////////////////////////////////////////
        // Aquisition process
        /////////////////////////////////////////////////
        else if(myMPIRank != server_mpi_rank) { 

		int spike_count=0;
                long total_spike      = 0;
                long total_recorded   = 0;
		float trigger_rate    = 0.0;
                float recording_ratio = 0.0; 
		int recorded_spike_count=0;
		int t=0;
		long time_index=0;
		long last_time_index=0;

		int i, j, k, m, n=0;
		MPI_Status mpi_status;

		// Initialise the DAQ. 
                *(notifier_host()) = "u183";

		if (daq_start() < 0)
		    return -1;

		// get data from DMA buffer
		notify(INFO, "Fetching data from DAQ ...");	
		while (halt==0)
		{
			if (loop_count >=999999)
			break;

                        // Get the time at loop start.
                        struct timeval tstart, tcopy, tsend, trecv, tstop;
                        gettimeofday(&tstart, NULL);
			
			// Get work_data from DMA buffer, so that we dont worry about read the same region of DMA again, or data be overwriten by DMA
			ret = daq_copy_data(work_data, work_data_length);
                        irq_count     = daq_irq();
                        buffer_offset = daq_offset();

                        // Get the time after data copy.
                        gettimeofday(&tcopy, NULL);
 
			if (0>ret){
				notify(ERROR, "Failed to get data from DAQ.");
				break;
			}

			//check if it is a new buffer (irq_count increased)
			if(irq_count>last_irq_count){
				if(irq_count-last_irq_count>1){
					notify(WARNING, "Buffer number (irq_count=%d) is out of sequence.", irq_count);
				}
				last_irq_count=irq_count;
			}

			//get DataSampleStdev at the beginning of every work_data, use large sample
			i=0;
			for(i=0;i<DataSampleLargeLength;i++){
				*(pDataSampleLarge+i)=(Ipp32f)work_data[i];
			}
			ippsStdDev_32f(pDataSampleLarge,DataSampleLargeLength,&DataSampleStdev,ippAlgHintFast);

			spike_count=0;
			//loop through work_data, step = spike_data_length 
			for(m=0;m<(long)work_data_length/spike_data_length;m++){
				//read sample_data (ipp32f) from work_data
				i=0;
				for(i=0;i<spike_data_length;i++){
					*(pDataSample+i)=(Ipp32f)work_data[i+m*spike_data_length];
				}


				//get DataSampleMax, DataSampleMaxIndex
				ippsMaxIndx_32f(pDataSample, DataSampleLength, &DataSampleMax, &DataSampleMaxIndex);
				//get DataSampleMean
				ippsMean_32f(pDataSample,DataSampleLength,&DataSampleMean,ippAlgHintFast);
				

				//test for spikes
				if( (fabs(DataSampleMax-DataSampleMean) >N*DataSampleStdev)) {

					if(spike_count>=spike_count_max) {
						notify(WARNING, "spike_count exceeded spike_count_max.");
						break;
					}
					
					//get spike_data from work_data
					if(m==0){
						memcpy(&spike_data[spike_count*spike_data_length], &work_data[0], spike_data_length*sizeof(unsigned char));
					}else if(m==(long)work_data_length/spike_data_length-1){
						memcpy(&spike_data[spike_count*spike_data_length], &work_data[(m-1)*spike_data_length], spike_data_length*sizeof(unsigned char));
					}else{
						//center the spike 
						memcpy(&spike_data[spike_count*spike_data_length], &work_data[m*spike_data_length + DataSampleMaxIndex - spike_data_length/2 ], spike_data_length*sizeof(unsigned char));
					}
					
					//save spike_time for the server
					spike_time[spike_count]=m*spike_data_length + DataSampleMaxIndex;

					//record spike time info 
					t=time(NULL);
					spike_info[spike_count*4+0]=t;
					spike_info[spike_count*4+1]=irq_count;
					spike_info[spike_count*4+2]=buffer_offset/spike_data_length + m +1;
					spike_info[spike_count*4+3]=DataSampleMaxIndex+1;

					spike_count++;
				}//end spike test
			}//end work_data

                        // Time before sending spike times.
                        gettimeofday(&tsend, NULL);

			//send spike_positions to server	
			MPI_Send(&spike_time[0],spike_count,MPI_LONG,server_mpi_rank,1,MPI_COMM_WORLD);

			//recv server filtering result 
			MPI_Recv(&spike_time[0], spike_count,MPI_LONG,server_mpi_rank,1,MPI_COMM_WORLD, &mpi_status);	
			
                        // Time after receiving master decision.
                        gettimeofday(&trecv, NULL);

                        int j=0; //count saved spikes
			for(i=0;i<spike_count;i++){ 
				if (spike_time[i] ==-1) {
					//copy spikes to save to file 
					memcpy(&spike_data_save[j*spike_data_length], &spike_data[i*spike_data_length], spike_data_length*sizeof(unsigned char));
					memcpy(&spike_info_save[j*4],&spike_info[i*4],4*sizeof(int));
					j++;
				}
			}
			recorded_spike_count=j;
                        dw_dump(datafilename, spike_data_length*recorded_spike_count, spike_data_save);
                        dw_dump(timefilename, 4*recorded_spike_count, spike_info_save);

                        //clear buffer for debugging purpose
                        memset(spike_data,0,spike_count_max*spike_data_length*sizeof(unsigned char));
                        memset(spike_time,0,spike_count_max*4*sizeof(int));

			//do statistics
                        total_spike    += spike_count;
                        total_recorded += recorded_spike_count;
                        trigger_rate    = ((float)(spike_count))/((float)work_data_length/200e6);
                        if (total_spike > 0)
                            recording_ratio = 100.0*((float)total_recorded)/((float)total_spike);
                        t=time(NULL);

                        //write to screen
                        notify(
                            INFO, 
                            "loop=%d, buf=%d, sub-buf=%d, trig_rate=%3.1f Hz, recording_ratio=%3.1f %%, total_recorded=%ld, stddev=%3.1f LSB",
                            loop_count, irq_count, buffer_offset/work_data_length, trigger_rate, recording_ratio, total_recorded, DataSampleStdev
                        );

                        // Get this iteration duration.
                        gettimeofday(&tstop, NULL);
                        double dtc = (tcopy.tv_sec-tstart.tv_sec)+1.0e-6*(tcopy.tv_usec-tstart.tv_usec);
                        double dta = (tsend.tv_sec-tcopy.tv_sec)+1.0e-6*(tsend.tv_usec-tcopy.tv_usec);
                        double dtd = (trecv.tv_sec-tsend.tv_sec)+1.0e-6*(trecv.tv_usec-tsend.tv_usec);
                        double dtw = (tstop.tv_sec-trecv.tv_sec)+1.0e-6*(tstop.tv_usec-trecv.tv_usec);
                        double t0 = tstart.tv_sec+1.0e-6*tstart.tv_usec;

			//log
                        dw_log(
                            logfilename, 
                            "%.3lf %.3lf %.3lf %.3lf %.3lf %d %d %d %d %3.1f",
                            t0, dtc, dta, dtd, dtw, loop_count, irq_count, spike_count, recorded_spike_count, DataSampleStdev
                        );

			loop_count++;
			MPI_Barrier(MPI_COMM_WORLD);
		}
	
		//close apex card
		daq_close();
		
	}//end aquisition process
	MPI_Finalize();
	return 0;
}


static void sig_int(int signo)
{
	notify(INFO, "Caught SIGINT, terminating program...");
	halt = 1;
	return;
}


//================================================================
int parse_inputs(int argsc, char** argsv, float* threshold, 
char** runid, int* multiplicity)
//================================================================
//
//  Parse the inputs arguments.
//
//================================================================
{
    char c;

    // Initialisation of mandatory arguments.
    *threshold    = 0.0;
    *runid        = NULL;
    *multiplicity = 0;

    // Parse the command line.
    while (1)
    {
        static struct option long_options[] =
        {
            {"help",          no_argument,       0, 'h'},
            {"threshold",     required_argument, 0, 't'},
            {"runid",         required_argument, 0, 'r'},
            {"multiplicity",  required_argument, 0, 'm'},
            DAQ_LONG_OPTIONS,
	    DW_LONG_OPTIONS,
	    LOGGER_LONG_OPTIONS
        };

        int option_index = 0;
        c = getopt_long(argsc, argsv, 
	    "ht:r:m:" DAQ_GETOPT_DESCRIPTOR DW_GETOPT_DESCRIPTOR LOGGER_GETOPT_DESCRIPTOR,
	    long_options, &option_index
	);

        if (c == -1)
            break;
        else if (c == 'h')
        {
            print_usage(argsv[0]);
            return(-1);
        }
        else if (c == 't')
            *threshold = strtod(optarg, NULL);
        else if (c == 'r')
            *runid = optarg;
        else if (c == 'm')
            *multiplicity = strtod(optarg, NULL);
        else
           daq_parse_option(c, optarg);
    }

    // Check if mandatory arguments where provided.
    if((*threshold == 0.0) || (*runid == NULL) || (*multiplicity == 0))
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
        "Usage: %s --threshold=[int] --runid=[int] --multiplicity=[int] %s %s %s\n"
        "* threshold:       the trigger threshold as multiple of standard deviation.\n"
        "* runid:           the runnumber for the data file name.\n"
        "* multiplicity:    the minimum number of coincident events required for recording.\n",
        proccess, daq_usage_text(), dw_usage_text(), logger_usage_text()
    );
    printf(daq_help_text());
    printf(dw_help_text());
    printf(logger_help_text());
}
