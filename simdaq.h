#ifndef SIMDAQ_H
#define SIMDAQ_H 1

#include "apex_tools.h" 


#define SIMDAQ_SIZE (256*1024)


int simdaq_init();
int simdaq_synchronise();
int simdaq_counter();
unsigned char* simdaq_data();
int simdaq_copy_data(unsigned char* data, int length);
int simdaq_close();

int simdaq_irq();
int simdaq_offset();

#endif
