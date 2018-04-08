////////////////////////////////////////////////////////////
//apex_tools.c
//
//a wrapper for apex driver 
//provide end user with init(), getData(), close() functions
//
///////////////////////////////////////////////////////////

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <time.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "apex_tools.h"
#include "logger.h"
#include "notifier.h"

struct {
    int last_irq;
    int current_irq;
    unsigned long offset;
    unsigned char* ping_buf;
    unsigned char* pong_buf;
} apex_tools_ctl = 
{
    0,
    0,
    0,
    NULL,
    NULL
};


unsigned char* get_ping()
{
    return apex_tools_ctl.ping_buf;
}


unsigned char* get_pong()
{
    return apex_tools_ctl.pong_buf;
}


int joinApex(int* pfd)
{
    // Open the apex device.
    //===
    *pfd = open( "/dev/apex", O_RDONLY );
    if (*pfd < 0)
    {
      notify(ERROR, "Failed to open /dev/apex!");
      return(-1);
    }

    // map physical DMA buffer to user space
    apex_tools_ctl.ping_buf = mmap(0, DMA_SIZE, PROT_READ, MAP_SHARED, *pfd, 0);
    apex_tools_ctl.pong_buf = mmap(0, DMA_SIZE, PROT_READ, MAP_SHARED, *pfd, DMA_SIZE);

    if (apex_tools_ctl.ping_buf == MAP_FAILED || apex_tools_ctl.pong_buf == MAP_FAILED) {
        notify(ERROR, "Failed to map physical ping-pong buffers out.");
        return(-1);
    }

    return 0;
}


////////////////////////////////////////////////////////
//initApex(): prepare the Apex card 
//
//no matter what state the card is at, this should 
//reset it with the right parameters and the card 
//should be ready for getApexData() call
//
//parameters: 
//int *pfd - pointer to the filedevice initApex opened
///////////////////////////////////////////////////////
int initApex(int *pfd)
{
	int ret,set,count,irq;	
	time_t start_time, stop_time; // time counter
	
	apex_reg_t apex_reg;
	
	// open device 
	*pfd = open("/dev/apex", O_RDONLY);
	if (*pfd < 0){
		notify(ERROR, "Failed to open device /dev/apex.");
		return(-1);
	}

	// some how trigger does not work if program exit abnormally 
	if (ret = ioctl(*pfd, IOCTL_APEX_STOP_TRANS, NULL)){
		notify(ERROR, "Failed to stop DMA transfer!");
		return(-1);
	}

	// reset card 
	apex_reg.select =1;
	apex_reg.offset =0;
	apex_reg.value = 3;
        if (ret = ioctl(*pfd, IOCTL_APEX_SET_REG, &apex_reg)){
		notify(ERROR, "Failed to reset Apex card!");
		return(-1);
	
	}

	usleep(1000000);

#if 1 
	// reset 5064 bridge contol register 
	// we use this to clear INTA (which left unclen by last DMA transfer)
	apex_reg.select = 0;
	apex_reg.offset = 0xe8;
	apex_reg.value = 1;
        if (ret = ioctl(*pfd, IOCTL_APEX_SET_REG, &apex_reg)){
		notify(ERROR, "Failed to reset Apex card!");
		return(-1);
	}
#endif

#if 0
	apex_reg.select = 0;
	apex_reg.offset = 0xe0;
	apex_reg.value = 0;
	if (ret = ioctl(*pfd, IOCTL_APEX_SET_REG, &apex_reg)){
		notify(ERROR, "Failed to reset Apex card!");
		return(-1);
	}
#endif


	//need to wait long enough for clock to lock phase
	usleep(1000000);

	// set delay between DMA buffer write 	
	apex_reg.select =1;
	apex_reg.offset = 0x20;
	apex_reg.value = 0x18000;

	if (ret = ioctl(*pfd, IOCTL_APEX_SET_REG, &apex_reg)){
		notify(ERROR, "Failed to set delay between DMA buffer write.");
		return(-1);
	}
	
	//set DMA timeout
	apex_reg.select = 1;
	apex_reg.offset = 0x18;
	apex_reg.value = 0x8000;

	if (ret = ioctl(*pfd, IOCTL_APEX_SET_REG, &apex_reg)){
		notify(ERROR, "Failed to set DMA timout.");
		return(-1);
	}
	
	// set trigger mode 
	set = 1; // 1=external 
	if (ret = ioctl(*pfd, IOCTL_APEX_TRIG_MODE, &set)){
		notify(ERROR, "Failed to set trigger mode.");	
		return(-1);
	}
	
	// set DMA count max 
	set =0xffffffff ;	
	if (ret = ioctl(*pfd, IOCTL_APEX_DMA_COUNT, &set)){
		notify(ERROR, "Failed to set DMA count max.");	
		return(-1);
	}
	
	apex_reg.select = 1;
	apex_reg.offset = 0x0;
	apex_reg.value = 0x26;

	if (ret = ioctl(*pfd, IOCTL_APEX_SET_REG, &apex_reg)){
		notify(ERROR, "Failed to set 0628 mode 1.");
		return(-1);
	}
	

	apex_reg.select = 1;
	apex_reg.offset = 0x08;
	apex_reg.value = 0xb;

	if (ret = ioctl(*pfd, IOCTL_APEX_SET_REG, &apex_reg)){
		notify(ERROR, "Failed to set 0628 mode 2.");
		return(-1);
	}
	
	if (ret = ioctl(*pfd, IOCTL_APEX_GET_IRQ, &irq)) {
		notify(ERROR, "Failed to get DMA transfer IRQ.");
		return(-1);
	}


	//
	// map physical DMA buffer to user space
	// mmap
	//
	apex_tools_ctl.ping_buf = mmap(0, DMA_SIZE, PROT_READ, MAP_SHARED, *pfd, 0);
	apex_tools_ctl.pong_buf = mmap(0, DMA_SIZE, PROT_READ, MAP_SHARED, *pfd, DMA_SIZE); //note: here the 2nd DMA_SIZE must be the same as the first
	if (apex_tools_ctl.ping_buf == MAP_FAILED || apex_tools_ctl.pong_buf == MAP_FAILED) {
		notify(ERROR, "Failed to map physical ping-pong buffers out.");
		return(-1);
	}

	//clear all DMA buffers
        memset(apex_tools_ctl.ping_buf, 0, DMA_SIZE);
	memset(apex_tools_ctl.pong_buf, 0, DMA_SIZE);

	usleep(10000);

	// start DMA transfer 
	if (ret = ioctl(*pfd, IOCTL_APEX_START_TRANS, NULL)){
		notify(ERROR, "Failed to start DMA transfer.");
		return(-1);
	}
	
	    
        notify(WARNING, "Please trigger the Apex card (will wait %d sec before exit)...", TRIGGER_WAIT_TIME);
        if (*(notifier_host()) != NULL)
        {
            char hostname[8] = "u000";
            gethostname(hostname, sizeof(hostname));
            send_notification("%s TRIGGER?", hostname);
        }
	
	// When trigger button is pressed DMA transfer should start 
	// when Apex card completed transfer data
	// to a DMA buffer, it will increase irq by 1 
	// now we wait for irq to become >0 
	//
	time(&start_time); //mark wait start time	
	while(1){
		if (ret = ioctl(*pfd, IOCTL_APEX_GET_IRQ, &irq)) {
			notify(ERROR, "Failed to get DMA transfer IRQ.");
			return(-1);
		}else if (irq==0){
			//
			//check DMA wait time out
			//the wait time is 100 second
			//
			time(&stop_time); //mark wait stop time
			if ((stop_time - start_time)>TRIGGER_WAIT_TIME) {
				notify(ERROR, "Failed to trigger DMA transfer: time out.");
				return(-1);
			}
			
		}else if (irq>0){
			notify(DEBUG, "DMA transfer started. irq=%d", irq);
			break;	// We have data in DMA buffer :)
		}else {
			notify(ERROR, "Unexpected DMA transfer IRQ value %d.",irq);
			return(-1);
		}
		usleep(10000);
	}
	return 0;
}
		
///////////////////////////////////////////////////////////
//getApexRawData(): read a block of data from DMA buffer
//
//note:
//	two integer meta data are appended to the end of 
//	the out buffer
//
//parameters: 
//char *pdata - pointer to the out buff (meta data
//		 will be appended to its tail)
//int data_length - the out put buffer length 
//		    (not including the meta data tail)
//int *pfd - pointer to the filedevice initApex opened
///////////////////////////////////////////////////////////	
int getApexRawData(unsigned char *pData, int data_length,int *irq_count,int *buffer_offset, int *pfd) {
	int ret, irq;
	int i, count;

	time_t start_time, stop_time; // time counter
	
	if ((data_length > DMA_SIZE) || (data_length < 0) || (DMA_SIZE % data_length != 0))	{
		notify(ERROR, "Invalid data_length!");
		return(-1);
	}

	// select DMA buffer 
	if (ret = ioctl(*pfd, IOCTL_APEX_GET_IRQ, &irq)) {
		notify(ERROR, "Failed to get DMA transfer IRQ.");
		return(-1);
	}else if (irq>0){
		notify(DEBUG, "irq=%d,last_irq = %d, offset = %ld", irq, apex_tools_ctl.last_irq, apex_tools_ctl.offset);
	}else {
		notify(ERROR, "Unexpected DMA transfer IRQ value %d.",irq);
		return(-1);
	}
	
	//
	// before each data read, we need to determin which DMA buffer to read.
	// we do this by reading the PCI bus irq counter, 
	// an irq increament means there was a PCI interupt call,
	// and we assume only a DMA transfer completion generate the interupt call.
	// the irq start from 0, when the first DMA buffer (ping) is ready the irq is 1,
	// when the seond DMA buffer (pong) is ready the irq increment to 2,
	// so on and so forth ...
	// odd number of irq means pingbuff is ready
	// even number of irq means pongbuff is ready
	// last_irq is the DMA buffer where I got my last data from 
	// if the irq incremented(new buffer ready), we ALWAYS read from 
	// the beginning of the new buffer
	// if the irq stays the same, we continue read until the end of the 
	// current buffer is reached, then we wait for the next irq increament
	//
	if (irq > apex_tools_ctl.last_irq) { // new DMA buffer is ready 
		apex_tools_ctl.offset = 0;
	}else if (irq==apex_tools_ctl.last_irq) { // no new DMA buffer 
		//
		// in this case, the DMA data transfer could still be going normally 
		// or could be stopped for some reason. any way,
		// we will finish reading our current DMA buffer first
		// and we need to watch that the end of current DMA buffer is reached
		//
		
		if (apex_tools_ctl.offset == DMA_SIZE) { // end of DMA buffer reached
			//
			//this happens when data processing is faster than data aquisition
			//
			time(&start_time); //mark wait start time	
			notify(DEBUG, "irq=%d: Waiting for next DMA buffer to be ready ...", irq);

			while (1) {

				//
				//need a timer to break out the wait loop
				//because the next DMA buffer might never happen
				//
		
				if (ret = ioctl(*pfd, IOCTL_APEX_GET_IRQ, &irq)) {
					notify(ERROR, "Failed to get DMA transfer IRQ.");
					return(-1);
				}
		
				if (apex_tools_ctl.last_irq != irq) { // next buffer is ready 
					apex_tools_ctl.offset = 0;
					break;
				}
				//
				//check DMA wait time out
				//the wait time is 2 second
				//
				time(&stop_time); //mark wait stop time	
				if ((stop_time - start_time)>2) {
					notify(ERROR, "Fatal error, DMA wait time out.");
					notify(ERROR, "Please check if the DMA LED on the Apex card is still on.");
					return(-1);
				}
				usleep(1000);
			}
		}
	}else {
		//
		//this should never happen!
		//
		notify(ERROR, "Fatal error. Unexpected DMA transfer IRQ value %d. (last_irq=%d)", irq, apex_tools_ctl.last_irq);
		return(-1);
	}
	
	//
	//read data from DMA buffer that correspond to the current_irq 
	//

	notify(DEBUG, "DMA transfer irq= %d, last_irq=%d, offset=%d", irq, apex_tools_ctl.last_irq, apex_tools_ctl.offset);
	if ((irq)%2 == 0){ // pong
		//
		//which DMA buffer is available?
		//the current_irq is irq.
	 	//ping buffer irq start as 1, then always be odd 	
	 	//pong buffer irq start as 2, then always even
	 	//
		memcpy(pData,(apex_tools_ctl.pong_buf+apex_tools_ctl.offset),sizeof(unsigned char)*data_length);
	}else{	// ping
		memcpy(pData,(apex_tools_ctl.ping_buf+apex_tools_ctl.offset),sizeof(unsigned char)*data_length);
	}
	*irq_count=irq;
	*buffer_offset=(int)apex_tools_ctl.offset;

	//
	//append meta data to the end of our buffer
	//note that the meta data is of int type!
	//
	//p_irq = pData+data_length; 
	//p_offset = pData+data_length+2;
	//*p_irq = irq;
	//*p_offset = apex_tools_ctl.offset;
		
	//
	//clear the buffer after reading
	//if we have continuse zeros in our data, something must be wrong
	//but this memset is painfully slow!!!!!!!!!!!!!
	//
/*	
 	if (irq%2==0){ //pong
		memset(apex_tools_ctl.pong_buf+apex_tools_ctl.offset, 0, data_length);
	}
	else{	// ping
		memset(apex_tools_ctl.ping_buf+apex_tools_ctl.offset, 0, data_length);
	}
*/
	//
	//prepare for the next data block read
	//
	apex_tools_ctl.last_irq=irq;
	apex_tools_ctl.offset += data_length;

	return 0;
}


///////////////////////////////////////////////
//closeApex(): shutdown the card elegantly 
// 
//
// important note: 
//	the user application must handle interrupt 
//	signal such as SIGINT and SIGTERM gracefully
//	(close the apex device and release irq)
//	otherwise system may hang.
//
//parameters: 
//int *pfd - pointer to the filedevice initApex opened
///////////////////////////////////////////////
int closeApex(int *pfd)
{
	int ret;	
	apex_reg_t apex_reg;

	notify(INFO, "Closing Apex card ...");
	
	if (ret = ioctl(*pfd, IOCTL_APEX_STOP_TRANS, NULL)){
		notify(ERROR, "Can't stop DMA transfer correctly!");
	}
#if 1
	// reset 5064 bridge contol register 
	// we use this to clear INTA (which left unclen by last DMA transfer)
	apex_reg.select = 0;
	apex_reg.offset = 0xe8;
	apex_reg.value = 1;
        if (ret = ioctl(*pfd, IOCTL_APEX_SET_REG, &apex_reg)){
		notify(ERROR, "Failed to reset Apex card!");
		return(-1);
	}
#endif

	// reset card 
	apex_reg.select =1;
	apex_reg.offset =0;
	apex_reg.value = 3;
        if (ret = ioctl(*pfd, IOCTL_APEX_SET_REG, &apex_reg)){
		notify(ERROR, "Failed to reset Apex card!");
		return(-1);
	}

	close(*pfd);
	return 0;
}


//=====================================================================
int synchroniseWithApex(int* pfd)
//=====================================================================
//
//  Synchronize with a buffer switch.
//
//=====================================================================
{
    int ret;
    int irq_count = apex_tools_ctl.current_irq;

    while (apex_tools_ctl.current_irq == irq_count)
    {
        if (ret = ioctl(*pfd, IOCTL_APEX_GET_IRQ, &irq_count))
        {
            notify(ERROR, "Failed to get DMA transfer IRQ.");
            return(-1);
        }
        else 
        {
            // Check irq number
            //===
            if (irq_count < 0)
            {
                notify(ERROR, "Wrong irq count: irq_count=%d.", irq_count);
                return(-1);
            }
            else if (irq_count == 0)
            {
                notify(ERROR, "DMA not started yet." );
                return( -1 );
            }
        }
    }

    apex_tools_ctl.current_irq = irq_count;
  
    return 0;
}


//=====================================================================
unsigned char* iddleApexBuffer()
//=====================================================================
//
//  Get the iddle buffer corresponding to the last irq.
//
//=====================================================================
{
    if ((apex_tools_ctl.current_irq%2) == 0) // pong
      return apex_tools_ctl.pong_buf;
    else  // ping
      return apex_tools_ctl.ping_buf;
}


//=====================================================================
int getApexIRQ(int* pfd)
//=====================================================================
//
//  Get the current irq count.
//
//=====================================================================
{
    int ret = ioctl(*pfd, IOCTL_APEX_GET_IRQ, &apex_tools_ctl.current_irq);
    if (ret < 0)
        return ret;

    return apex_tools_ctl.current_irq;
}
