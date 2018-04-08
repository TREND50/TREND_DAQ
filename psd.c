//======================================================================================
//
//  psd.c
//
//======================================================================================
//
//  
//
//======================================================================================
#include <syslog.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <getopt.h>
#include <stdio.h>

#include "ipps.h"
#include "daq_i.h"
#include "data_writer.h"
#include "logger.h"


//======================================================================================
// Macros
//======================================================================================
#define SLICE_SIZE	512
#define N_IPARAMETERS	4
#define N_FPARAMETERS   2
#define MAGIC_SCALING   0.1915


//======================================================================================
// Handle the terminate interuption to ensure reliable data
//======================================================================================
static void sig_int( int );
int halt = 0; // Stop flag


//======================================================================================
// Routines for the parsing of input arguments.
//======================================================================================
int parse_inputs(int argsc, char** argsv, float* statistic,
float* period, int* maxiter, char** runid);
void print_usage(char* proccess);


//======================================================================================
int main( int argsc, char *argsv[] )
//======================================================================================
{
  int nslice         = daq_buffer_size()/SLICE_SIZE; 
  int start_apex     = 0;    // 0) Slave mode, 1) Master mode
  int period         = 1;    // Period over which data are accumulated
  int ret;                   // Return value
  int irq_count      = 0;    // Use this to track which data buffer we are at
  int last_irq_count = 0;
  int irq_after      = 0;
  float scaling      = MAGIC_SCALING;
  int Nsample        = -1;
  char timefilename[] = "PSD_time.bin";
  char datafilename[] = "PSD_data.bin";
  char hostname[ 65 ];
  int iparameters[ N_IPARAMETERS ];
  float fparameters[ N_FPARAMETERS ];
  char *runnumber;
  int  islice, nstat;

  // Allocate memory for FFT
  //===
  const int FFT_order = (int)( log( SLICE_SIZE )/log( 2 ) + 0.05 );
  const int FFT_size  = (int)( SLICE_SIZE/2+1 );
  const int CCS_size  = (int)( SLICE_SIZE+2 ); 
  IppsFFTSpec_R_32f* pFFT;
  IppStatus status = ippsFFTInitAlloc_R_32f( &pFFT, FFT_order, IPP_FFT_DIV_FWD_BY_N, ippAlgHintAccurate );
  if ( status != ippStsNoErr )
  {
    notify(ERROR, "Could not allocate FFT structure memory. Aborting." );
    return( 0 );
  }

  int FFT_buffer_size = 0;
  status = ippsFFTGetBufSize_R_32f( pFFT, &FFT_buffer_size );
  Ipp8u* FFT_buffer = ippsMalloc_8u( FFT_buffer_size );
  if ( FFT_buffer == NULL )
  {
    notify(ERROR, "Could not allocate memory for FFT buffer. Aborting." );
    return( 0 );
  }

  Ipp32f* FFT_data = ippsMalloc_32f( CCS_size );
  Ipp32f* psd      = ippsMalloc_32f( CCS_size );
  Ipp32f* FFT_win  = ippsMalloc_32f( SLICE_SIZE );    
  if ( ( FFT_data == NULL ) || ( psd == NULL ) || ( FFT_win == NULL ) )
  {
     notify(ERROR, "Could not allocate memory for FFT data. Aborting." );
    return( 0 );
  }
  ippsSet_32f( 1, FFT_win, SLICE_SIZE );     
  ippsWinHann_32f_I( FFT_win, SLICE_SIZE );


  //====================================================================================
  // Parse arguments
  //====================================================================================
  float statistic = 1e5;
  float period_s  = 0;

  if (parse_inputs(argsc, argsv, &statistic, &period_s, &Nsample, &runnumber) < 0)
      return(0);
  
  period = (int)(period_s/1.3422 + 0.4999);
  if ( period <= 0 )
      period = 1;
  scaling = statistic*MAGIC_SCALING/1e5/period;


  //====================================================================================
  // Configure I/Os
  //====================================================================================       
  gethostname( hostname, sizeof( hostname ) );
  int irun = atoi(runnumber);
  int ihost = atoi(hostname+1);

  dw_initialise(irun, ihost);
  dw_clear(datafilename);
  dw_clear(timefilename);


  //====================================================================================
  // Start the DAQ
  //====================================================================================
  if (daq_start() < 0)
      return 0;


  //====================================================================================
  // Remap terminate interupt
  //====================================================================================
  signal( SIGINT, sig_int );


  //====================================================================================
  // Acquisition loop on data buffers
  //====================================================================================
  srand48( time( NULL ) );
  int count = 0;
  int nacc  = 0;
  while ( halt == 0 )
  {
    // Synchronize with a buffer switch.
    //===
    daq_synchronise();
    irq_count = daq_counter();

    // Check for interupt
    //===
    if (halt != 0)
      break;
      
    // Map the iddle buffer
    //===
    unsigned char* dma_buf = daq_data();


    //==================================================================================
    // Analyse the sub buffers
    //==================================================================================
    if ( nacc == 0 ) // Initialisation
    {
      nstat = 0;
      memset( fparameters, 0x0, N_FPARAMETERS*sizeof( float ) );
      ippsZero_32f( psd, CCS_size );
    }

    // Accumulation
    //===
    for ( islice = 0; islice < nslice; islice++ )
    {
      if ( drand48() < scaling ) 
      {
        float p[ 2 ];
        ippsConvert_8u32f( dma_buf, FFT_data, SLICE_SIZE );
        ippsMeanStdDev_32f( FFT_data, SLICE_SIZE, p, p+1,
          ippAlgHintAccurate );
        fparameters[ 0 ] += p[ 0 ];
        fparameters[ 1 ] += p[ 1 ]*p[ 1 ];
        ippsMul_32f_I( FFT_win, FFT_data, SLICE_SIZE );
        ippsFFTFwd_RToCCS_32f_I( FFT_data, pFFT, FFT_buffer );
        ippsSqr_32f_I( FFT_data, CCS_size );
        ippsAdd_32f_I( FFT_data, psd, CCS_size );
        nstat++;
      }
      dma_buf += SLICE_SIZE;
    }
    nacc++;


    //==================================================================================
    // Write to file & Verbose
    //==================================================================================
    if ( nacc >= period )
    {
      nacc = 0;
      if ( nstat == 0 )
        continue;

      for ( islice = 0; islice < FFT_size; islice++ )
        psd[ 2*islice ] += psd[ 2*islice +1 ];
      for ( islice = 1; islice < FFT_size; islice++ )
        psd[ islice ] = psd[ 2*islice ];
      Ipp32f norm = 1.0/nstat;
      ippsCopy_32f( fparameters, psd+FFT_size, N_FPARAMETERS );
      ippsMulC_32f_I( norm, psd, FFT_size+N_FPARAMETERS );
      psd[ FFT_size+1 ] = sqrt( psd[ FFT_size+1 ] );

      irq_after = daq_counter();
      iparameters[ 0 ] = time( NULL );
      iparameters[ 1 ] = irq_count;
      iparameters[ 2 ] = irq_after;
      iparameters[ 3 ] = nstat;

      dw_dump(timefilename, N_IPARAMETERS, iparameters);
      dw_dump(datafilename, FFT_size-1+N_FPARAMETERS, psd+1);

      notify(INFO, "irq_count=%d/%d, stat=%d.", 
      iparameters[ 1 ], iparameters[ 2 ], iparameters[ 3 ] );
      count++;
    }

    // Check the number of samples recorded
    //===
    if (( count >= Nsample ) && ( Nsample > 0 ))
      break;
  }


  //====================================================================================
  // Close $ free
  //====================================================================================
  ippsFFTFree_R_32f( pFFT );
  ippsFree( FFT_buffer );
  ippsFree( FFT_data );
  ippsFree( psd );
  ippsFree( FFT_win );

  daq_close();

  return( 0 );
}


//======================================================================================
static void sig_int( int signo )
//======================================================================================
{
  notify(WARNING, "Caught SIGINT\n\tTerminating program ...");
  halt = 1;
  return;
}


//================================================================
int parse_inputs(int argsc, char** argsv, float* statistic,
float* period, int* maxiter, char** runid)
//================================================================
//
//  Parse the inputs arguments.
//
//================================================================
{
    char c;

    // Initialisation of mandatory arguments.
    *runid     = NULL;


    // Parse the command line.
    while (1)
    {
        static struct option long_options[] =
        {
            {"help",       no_argument,       0, 'h'},
            {"statistic",  required_argument, 0, 's'},
            {"period",     required_argument, 0, 'p'},
            {"runid",      required_argument, 0, 'r'},
            {"maxiter",    required_argument, 0, 'm'},
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
        else if (c == 's')
            *statistic  = strtod(optarg, NULL);
        else if (c == 'p')
            *period = strtod(optarg, NULL );
        else if (c == 'r')
            *runid = optarg;
        else if (c == 'm')
            *maxiter = atoi(optarg);
        else
        {
            daq_parse_option(c, optarg);
            dw_parse_option(c, optarg);
            logger_parse_option(c, optarg);
        }
    }


    // Check if mandatory arguments where provided.
    if (*runid == NULL)
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
        "Usage: %s --runid=[int] (--period=[float]) (--statistic=[float]) (--maxiter=[int]) %s %s %s\n"
        "* runid:           the runnumber for the data file name.\n"
        "* period:          the periodicity of the psd measurement, in unit second. Defaults to 1.3 s.\n"
        "* statistic:       the statistic used for the psd. Defaults to 1e5.\n"
        "* maxiter:         the maximum number of psd measurements. A negative value indicates infinite looping. Defaults to -1.\n",
        proccess, daq_usage_text(), dw_usage_text(), logger_usage_text()
    );
    printf(daq_help_text());
    printf(dw_help_text());
    printf(logger_help_text());
}
