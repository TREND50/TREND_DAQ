#ifndef APEX_TOOLS_H
#define APEX_TOOLS_H 1

/*
 * Defined some constants for Apex accessing.
 */

typedef struct {
        unsigned int select; /* 0 is select QLogic register, 1 is select Apex register */
        unsigned int offset; /* register offset */
        unsigned long value; /* register value */
} apex_reg_t;

typedef struct {
        unsigned int select; /* 0 is select buffer 0, 1 is select buffer 1 */
        unsigned int offset; /* the buffer offset where the user want to start accessing */
        unsigned int length; /* buffer length, Be careful, the system will copllapse if the length is more than the size of buffer */
        unsigned char *buf;
} apex_buf_t;

#define APEX_MINOR 160

#define IOCTL_APEX_START_TRANS  _IO('z', 0x01) /* start dma transfer */
#define IOCTL_APEX_STOP_TRANS   _IO('z', 0x02) /* stop dma transfer */
#define IOCTL_APEX_ADC_RESET    _IO('z', 0x03) /* reset ADC */
#define IOCTL_APEX_BOARD_RESET  _IO('z', 0x04) /* reset board */

#define IOCTL_APEX_TRIG_MODE    _IOW('z', 0x10, unsigned int) /* set trigger mode for APEX card */
#define IOCTL_APEX_DMA_SIZE     _IOW('z', 0x11, unsigned long)/* set dma size user expect to use */
#define IOCTL_APEX_DMA_COUNT    _IOW('z', 0x12, unsigned int) /* set the transfer counts user expect */

#define IOCTL_APEX_GET_IRQ      _IOR('z', 0x21, unsigned long) /* get the interrupt counts since starting transfer */

#define IOCTL_APEX_SET_REG      _IOWR('z', 0x30, apex_reg_t)
#define IOCTL_APEX_GET_REG      _IOWR('z', 0x31, apex_reg_t)
#define IOCTL_APEX_GET_BUF      _IOWR('z', 0x32, apex_buf_t)

#define DMA_SIZE (256*1024*1024)
// #define SETNUM 100	

// Wait time for triggering the Apex card, in unit second.
#define TRIGGER_WAIT_TIME 120

/*
 *  Interface functions to Apex.
 */

unsigned char* get_ping();
unsigned char* get_pong();
int joinApex(int* pfd);
int initApex(int *pfd);
int synchroniseWithApex(int* pfd);
unsigned char* iddleApexBuffer();
int getApexIRQ(int* pfd);
int getApexRawData(unsigned char *pData, int data_length,int *irq_count, int *offset, int *pfd);
int closeApex(int *pfd);

#endif
