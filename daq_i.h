#ifndef DAQ_I_H
#define DAQ_I_H 1

#define DEFAULT_DAQ_MODE Apex
#define DEFAULT_DAQ_TYPE Master

#define DAQ_LONG_OPTIONS \
    {"daqmode", required_argument, 0, 'M'},\
    {"daqtype", required_argument, 0, 'D'},\
    {"simopts", required_argument, 0, 'O'}

#define DAQ_GETOPT_DESCRIPTOR "M:D:O:" 

enum DaqType {Apex, Sim};
enum DaqMode {Master, Slave};

int daq_start();
int daq_join();
int daq_init();
int daq_synchronise();
int daq_counter();
unsigned char* daq_data();
int daq_copy_data(unsigned char* data, int length);
int daq_close();

int daq_irq();
int daq_offset();

int* daq_type();
int* daq_mode();
char** daq_simopts();

int daq_parse_option(char c, char* optarg);
char* daq_help_text();
char* daq_usage_text();

int daq_buffer_size();

#endif
