#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "selector.h"
#include "logger.h"


#define USE_IPPS 1


#if(USE_IPPS == 1)
    #include "ipp.h"
#endif


struct {
    float threshold;
    int   multiplicity;
    char* detconfig;
    int   delay[MAX_ANTENNA];
    int   distance[MAX_ANTENNA][MAX_ANTENNA];
} selector_ctl = 
{
    6.0,
    4,
    "/home/pastsoft/trend/daq/config/22-02-12.cfg"
};


float* selector_threshold()
{
    return &selector_ctl.threshold;
}


int* selector_multiplicity()
{
    return &selector_ctl.multiplicity;
}


char** selector_detconfig()
{
    return &selector_ctl.detconfig;
}


int selector_initialise(int n_antenna, int antenna_id[MAX_ANTENNA])
{
    // Read delays and distances.
    float delay[MAX_ANTENNA];
    float distance[MAX_ANTENNA][MAX_ANTENNA];

    FILE* fid = fopen(selector_ctl.detconfig, "r");
    if (fid == NULL)
    {
        notify(ERROR, "Couldn't open configuration file %s", selector_ctl.detconfig);
        return(-1);
    }

    for (int i = 0; i < MAX_ANTENNA; i++)
        fscanf(fid, "%f", &delay[i]);
    
    for (int i = 0; i < MAX_ANTENNA; i++) for (int j = 0; j < MAX_ANTENNA; j++)
            fscanf(fid, "%f", &distance[i][j]);

    fclose(fid);


    // Compute the rounded and remaped values in unit sample.
    for (int i = 0; i < n_antenna; i++)
    {
        int ii = antenna_id[i];
        selector_ctl.delay[i] = (int)(delay[ii]+0.49999);
        
        for (int j = 0; j < n_antenna; j++)
        {
            int jj = antenna_id[j];
            selector_ctl.distance[i][j] = (int)(distance[ii][jj]/CONSTANT_C0/CONSTANT_TS*SELECTOR_T_WINDOW+0.49999);
        }
    }

    return(0);
}


#if(USE_IPPS == 1)
float slipps_find_spikes(int n_data, unsigned char* data, int* n_time, int time[MAX_SPIKE])
{
    Ipp8u* pd = (Ipp8u*)data;
    int i, imax = n_data/SAMPLE_SIZE;

    Ipp32f N = (Ipp32f)selector_ctl.threshold;

    float stddev = 0.0;
    int   nstd   = 0;
    int it       = 0;
    for (i = 0; i < imax; i++)
    {
        // Check if maximum number of spikes was reached.
        if (it == MAX_SPIKE)
            break;

        // Copy data locally.
        Ipp32f p[SAMPLE_SIZE], mu, sigma, amax;
        int    jmax;
        ippsConvert_8u32f(pd, p, SAMPLE_SIZE);


        // Compute the sample statistics.
        ippsMeanStdDev_32f(p, SAMPLE_SIZE, &mu, &sigma, ippAlgHintFast);
        stddev += sigma*sigma;
        nstd++;


        // Compute the amplitude.
        ippsSubC_32f_I(mu, p, SAMPLE_SIZE);
        ippsAbs_32f_I(p, SAMPLE_SIZE);
        ippsMaxIndx_32f(p, SAMPLE_SIZE, &amax, &jmax);


        // Check spike(s).
        if (amax > N*sigma)
        {
            int ti = i*SAMPLE_SIZE + jmax;
            if ((it == 0) || (ti-time[it-1] >= POST_SPIKE_DEAD_TIME))
            {
                time[it] = ti;
                it++;
            }
        }

        pd += SAMPLE_SIZE;
    }


    // Update the numbre of spikes.
    *n_time = it;

    
    // Averaged standard deviation.
    stddev = sqrt(stddev/nstd);

    return(stddev);
}
#endif


float selector_find_spikes(int n_data, unsigned char* data, int* n_time, int time[MAX_SPIKE])
{
    unsigned char* pd = data;
    int i, j, imax = n_data/SAMPLE_SIZE;

    float stddev        = 0.0;
    int   nstd          = 0;
    int   it            = 0;
    float sample_size_f = (float)SAMPLE_SIZE;
    for (i = 0; i < imax; i++)
    {
        // Check if maximum number of spikes was reached.
        if (it == MAX_SPIKE)
            break;
  
  
        // Copy data locally.
        unsigned char p[SAMPLE_SIZE];
        memcpy(p, pd, SAMPLE_SIZE*sizeof(unsigned char));
	
     
        // Compute the mean and standard deviation.
        int mu_i    = 0;
        int sigma_i = 0;
        for (j = 0; j < SAMPLE_SIZE; j++)
        {
            mu_i    += p[j];
            sigma_i += p[j]*p[j];  
        }
        float mu_f    = mu_i/sample_size_f;
        float sigma_f = sigma_i/sample_size_f - mu_f*mu_f;
        if (sigma_f > 0.0)
            sigma_f = sqrt(sigma_f);
        else
            sigma_f = 0.0;

        stddev += sigma_f*sigma_f;
        nstd++;

        sigma_f *= selector_ctl.threshold;
        if (sigma_f >= 255.0)
        {
            pd += SAMPLE_SIZE;
            continue;
        }

        unsigned char mu        = (unsigned char)mu_f;
        unsigned char threshold = (unsigned char)sigma_f;
       
        // Look for the sample with maximum amplitude.
        int           jmax = 0;
        unsigned char amax = 0;
        for (j = 0; j < SAMPLE_SIZE; j++)
        {

            unsigned char a;
            if (p[j] > mu) 
	        a = p[j] - mu;
            else 
	       a = mu - p[j];

            if (a > amax)
            {
                jmax = j;
                amax = a; 
            }
        }


        if (amax > threshold)
        {
            int ti = i*SAMPLE_SIZE + jmax;
            if ((it == 0) || (ti-time[it-1] >= POST_SPIKE_DEAD_TIME))
            {
                time[it] = ti;
                it++;
            }
        }


        pd += SAMPLE_SIZE;
    }

 
    // Update the numbre of spikes.
    *n_time = it;


    // Averaged standard deviation.
    stddev = sqrt(stddev/nstd);

    return(stddev);
}

int selector_find_coincidences(int n_antenna, int n_time[MAX_ANTENNA], int time[MAX_ANTENNA][MAX_SPIKE], char 
decision[MAX_ANTENNA][MAX_SPIKE])
{
    return 0;
}


#if(USE_IPPS == 1)
int slipps_find_coincidences(int n_antenna, int n_time[MAX_ANTENNA], int time[MAX_ANTENNA][MAX_SPIKE], char 
decision[MAX_ANTENNA][MAX_SPIKE])
{
    // Initialise decision.
    memset(decision, 0x0, MAX_ANTENNA*MAX_SPIKE);


    // Sort times.
    Ipp32s t[MAX_ANTENNA*MAX_SPIKE];
    Ipp32s antenna[MAX_ANTENNA*MAX_SPIKE];
    int    index[MAX_ANTENNA*MAX_SPIKE];

    int n_t = 0;
    Ipp32s *pt = t, *pa = antenna;
    for (int ia = 0; ia < n_antenna; ia++)
    {
        ippsCopy_32s(&time[ia][0], pt, n_time[ia]);
        ippsSubC_32s_ISfs(selector_ctl.delay[ia], pt, n_time[ia], 0);
        ippsSet_32s(ia, pa, n_time[ia]);

        pa  += n_time[ia]; 
        pt  += n_time[ia];
        n_t += n_time[ia];
    }

    ippsSortIndexAscend_32s_I(t, index, n_t);
    

    // Look for coincs.
    Ipp32s Ia0[MAX_ANTENNA*MAX_SPIKE], Ia1[MAX_ANTENNA*MAX_SPIKE];

    int nCoinc;    
    ippsZero_32s(Ia0, n_antenna);
    int j0, j1;
    for (j0 = 0; j0 < n_t-1; j0++)
    {
         int ant0 = antenna[index[j0]];
         ippsZero_32s(Ia1, n_antenna);
         nCoinc = 1;
         Ia1[ant0]++;

         for (j1 = j0+1; j1 < n_t; j1++)
         {
             int ant1 = antenna[index[j1]]; 

             if ((ant1 == ant0) || (t[j1]-t[j0] <= selector_ctl.distance[ant1][ant0]))
             {
                 if (Ia1[ant1] == 0)
                     nCoinc += 1;

                 Ia1[ant1]++;
             }
             else
                 break;
         }
         notify(DEBUG, "%s antennas in coinc.", nCoinc);
	 
         if (nCoinc >= selector_ctl.multiplicity)
         {
             for (int ia = 0; ia < n_antenna; ia++) if (Ia1[ia] > 0)
                 memset(&decision[ia][Ia0[ia]], 0x1, Ia1[ia]);

             j0 = j1-1;
             ippsAdd_32s_ISfs(Ia1, Ia0, n_antenna, 0);
         }
         else
             Ia0[ant0]++;
    }

    
    return 0;
}
#endif


int selector_parse_option(char c, char* optarg)
{
    if (c == 't')
        selector_ctl.threshold = strtod(optarg, NULL);
    else if (c == 'm')
        selector_ctl.multiplicity = strtod(optarg, NULL);
    else if (c == 'C')
    {
        if (strlen(optarg) > 0)
            selector_ctl.detconfig = optarg;
    }

    return 0;
}


char selectorhelp[] =
        "* threshold:       the trigger threshold as multiple of standard deviation.\n"
        "* multiplicity:    the minimum number of coincident events required for recording.\n"
        "* detconfig:       the detector configuration file: delays and distances.\n";

char* selector_help_text()
{
    return selectorhelp;
}


char selectorusage[] = "--threshold=[float] --multiplicity=[int] (-detconfig=[char*])";

char* selector_usage_text()
{
    return selectorusage;
}

