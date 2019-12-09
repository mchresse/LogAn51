/*
 * %ID: linux/drivers/char/la_ll.c
 *
 *  Copyright (C) 1994, Randolph Esser        
 */
/*****************************************************************************
 *
 * Dev.-Name: LA                                                 12.07.1994  
 * Common:                                                                   
 * 'la_ll.c' makes the opt. logic-analyser LOGAN16 transparent for initia-
 * lization the scanning HW, start scanning by external trigger and 
 * reading the actually filled scan-buffer.
 *
 * I used the same clean structure of high-level (la_hl.c) and low-level 
 * (la_ll.c) routines.                                                       
 *                                                                             
 * Randolph Esser   7/12/94                                                
 *                                                                             
 *****************************************************************************/



#include <linux/types.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/ctype.h>
#include <linux/kd.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/malloc.h>

#include <asm/segment.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>

#include <linux/la.h>			/* inclusive linux/la_def.h */


/****************************************************************************/
/* definitions and declarations: */
                                                                   


/****************************************************************************/
/* prototyping: */

int  logan_lseek(struct la_struct *, struct file *, off_t, int);                
int  logan_read(struct la_struct *, struct file *,
					   unsigned  char *, unsigned int);                
int  logan_write(struct la_struct *, struct file *,
					 	unsigned char *, unsigned int);               
int  logan_select(struct la_struct *, struct file *,
						int, select_table *);
int  logan_ioctl(struct la_struct *, struct file *,
        				unsigned int, unsigned long); 
int  logan_open(struct la_struct *, struct file *);
void logan_close(struct la_struct *, struct file *);                          

void logan_rdstatus(int, struct la_struct *);      


void logan_rdstatus( int, struct la_struct *);

static void log_initial(struct la_struct * );
static void log_reset(struct la_struct * );
static void start_scan( int, struct la_struct * );
static unsigned int  rd_scandata( unsigned int, unsigned char *,
								  struct la_struct * );
static unsigned int rd_extdata( struct la_struct * );
                        


/*****************************************************************************/
/* local functions: */

/*****************************************************************************/
/* SR-Name: logan_select(struct la_struct * lax, struct file * filp,         */
/*                    int sel_type, select_table * wait)                     */
/* Common : tells o.k. if the la-device is in ready-state.                   */
/*          (irq-handling was not attempt yet)                               */
/*                                                                           */
/* used SR  : logan_rdstatus() rd the LOGAN-status-port and update la_struct */
/*            printk() special kernel printing routine; video-direct-access  */
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : filp   current file-struct of device-access                    */
/*            sel_type = SEL_IN  for call: 'ready for read ?'                */
/*                     = SEL_OUT for call: 'ready for write ?'               */
/*                     = SEL_EX  for call: 'got an exception ?'              */
/*            wait   list of file-descriptors                                */
/*            lax    parameterfiled-ptr of current subdevice                 */
/* -OUT     :  -                                                             */
/* -RETURN  : errno  negativ = error; =0 device not ready for 'sel_type'-acc.*/
/*                                    =1 device ready for 'sel_type'-access  */
/* Variables                                                                 */
/* -external:  -                                                             */
/* -global  :  -                                                             */
/* -local   : minor  subdevice-nbr of LOGAN-device (1..256)                  */
/*                                                                           */
/*****************************************************************************/
int logan_select(struct la_struct * lax, struct file * file,
					    int sel_type, select_table *wait)
{
 int minor;

    minor = lax->minor;

	switch (sel_type)           /* dummy structure, historical */
	{	case SEL_IN:			/*  - insignificant here -     */
		case SEL_EX:
		case SEL_OUT:
		  return 1;				/* dummy return */
	}
	
    do                          /* wait for end of count of Scanning-process */
     { logan_rdstatus( minor, lax );
     } while(IS_LA_BUSY(minor));

#ifdef LA_DEBUG
    printk(" - special select-routine passed of la%d\n", minor);
#endif
    
	return 1;                              /* LOGAN-Card is ready for read() */
}



/*****************************************************************************/
/* SR-Name: logan_rdstatus(int minor, struct la_struct * lax)                */
/* Common : read LOGAN-Card-Status and put it in the la-structure            */
/*          (irq-handling was not attempt yet)                               */
/*                                                                           */
/* used SR  : inb()  read one byte of pc-port                                */
/*            printk() special kernel print-routine; video-direct-access     */
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : minor  current minor-nbr of subdevice                          */
/*            lax    parameterfiled-ptr of current subdevice                 */
/* -OUT     :  -                                                             */
/* -RETURN  :  -                                                             */
/*                                                                           */
/* Variables                                                                 */
/* -external:  -                                                             */
/* -global  :  -                                                             */
/* -local   : status buffer of LOGAN-status-port                             */
/*                                                                           */
/*****************************************************************************/
void logan_rdstatus(int minor, struct la_struct * lax)
{
unsigned int status;

  status = inb(IO_STAT);			/* read port-data     */

  lax->status = (unsigned char)status; /* put statusbyte into parameterfield */

#ifdef LA_DEBUG
  printk(" - current status of la%d is: %X\n", minor, status);
#endif
}


/*****************************************************************************/
/* SR-Name: logan_lseek(struct la_struct * lax, struct file * filp,          */
/*                    off_t offset, int origin)                              */
/* Common : get the new scanbuffer-counteraddress from la_struct and reload  */
/*          the scanbuffer-counter on LOGAN.                                 */
/*                                                                           */
/* used SR  : printk() special kernel print-routine; video-direct-access     */
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : lax    current parameterfield of subdevice                     */
/*            filp   current file-struct of device-access                    */
/*            offset = 'scansteps * 2' to start with scanning or reading     */
/*            origin base-address-mode: 0 start at 0x0000					 */
/*										1 start from current lax->scan_adr   */ 
/*										2 start from end (LA_BUF_MAX)backward*/ 
/* -OUT     :  -                                                             */
/* -RETURN  : errno  negativ = error; =0 addresscounter corectly updated     */
/*                                    =-1 something goes wrong               */
/* Variables                                                                 */
/* -external:  -                                                             */
/* -global  :  -                                                             */
/* -local   : minor  current subdevice-nbr of LOGAN-device                   */
/*            scanadr current address of scan-buffer in LOGAN-hw             */
/*                                                                           */
/*****************************************************************************/
int logan_lseek(struct la_struct * lax, struct file * filp,
					   off_t offset, int origin)                
{
 int minor;
 int  scanadr;			/* fully new address */


    minor = lax->minor;			/* get current minor-nbr */
	scanadr = lax->scan_adr;	/* get current scan-address-value */    

	switch (origin)				/* evaluate address-mode */
	 {case 0:	/* offset is relativ to base-address */
	 			scanadr = ((int)offset /2) & LA_SCANBUF_MAX;
				break;	
	  case 1:	/* offset is relative to current base-address */
	  			if(((scanadr + ((int)offset /2)) > LA_SCANBUF_MAX) |
	  			   ((scanadr + ((int)offset /2)) < 0 )) {
#ifdef LA_DEBUG	  
	  			  printk("la_lseek*: offset out of range in mode 1: 0x%X '\n",
 	  			          (unsigned int) offset * 2);
#endif	  		
   				  return (-1); 	/* Error: offset out of range */
				 }
	  			scanadr += ((int)offset /2);
				break;
	  case 2:	if((LA_SCANBUF_MAX + ((int)offset /2)) > LA_SCANBUF_MAX) {
#ifdef LA_DEBUG	  
	  			 printk("la_lseek*: offset out of range in mode 2: 0x%X '\n",
	  			         (unsigned int) offset * 2);
#endif	  		
	             return (-1);   /* Error: offset out of range */                
				 }                                     
	  			scanadr = LA_SCANBUF_MAX + ((int)offset /2);
				break;
	  default:  
#ifdef LA_DEBUG	  
	  			printk("la_lseek*: wrong lseek-mode as 'origin=%d '\n",origin); 
#endif	  		
	  			return (-1);				
	  			break;
	 }
	
	lax->scan_adr = (unsigned int) scanadr;	/* safe reloaded scanbuf-address */

#ifdef LA_DEBUG
    printk(" - special lseek-routine of la%d passed, ", minor);
    printk("with new scanbuf-address : 0x%X \n", lax->scan_adr);
#endif
	return (lax->scan_adr);        		/* return reached scan-buffer address */
}


/*****************************************************************************/
/* SR-Name: logan_read(struct la_struct * lax, struct file * filp,           */
/*                    unsigned char * buffer, unsigned int count)            */
/* Common : read 'count' byte data depend on lax->mode from scan-buffer or   */
/*          from external dataport.                                          */
/*                                                                           */
/* used SR  : printk() special kernel print-routine; video-direct-access     */
/*            log_reset()   prepare LOGAN-hw for read-access to ram          */
/*            start_scan()  start scanning ext.-port of LOGAN-hw             */
/*            rd_scandata() copy scanbuffer to user-space                    */
/*            printk() special kernel print-routine; video-direct-access     */
/*            inb()    read 1 byte from pc-port                              */
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : lax    current parameterfield of subdevice                     */
/*            filp   current file-struct of device-access                    */
/*            buffer user-bufferspace for scan-data, size = count bytes !    */
/*            count  size of user-buffer in Byte = nbr of byte to read       */
/* -OUT     :  -                                                             */
/* -RETURN  : errno  negativ = error; =0  scandata read without problems     */
/*                                    =-1 something goes wrong               */
/*                                    >0  nbr of read databyte               */
/* Variables                                                                 */
/* -external:  -                                                             */
/* -global  :  -                                                             */
/* -local   : scanadr current address of scan-buffer in scanning-mode        */
/*            rd_adr  current address of scan-buffer in read-mode            */
/*            extdata         buffer of 16-bit extern data-port              */
/*            pollmode =0 no busy-polling before rd-access -> fast blind-read*/
/*                     =1 wait if countend-ff in LOGAN-hw = 1 -> scan-end    */
/*            status  buffer of status-port of LOGAN-hw                      */
/*            timecnt loop-counter for timeout while polling for scan-end    */
/*            stepnbr nbr of scan-steps to be read(+scan); each 16bit-data   */
/*                                                                           */
/*****************************************************************************/
int logan_read(struct la_struct * lax, struct file * filp,
					   unsigned  char * buffer, unsigned int count)
{
 int minor;
 unsigned int  scanadr;			/* fully current scanaddress */
 unsigned int  rd_adr;			/* fully selected rd-start-address */
 unsigned int  extdata;         /* buffer for external dataport-read-access */
 int           pollmode;		/* =1 if polling mode selected */
 unsigned char status;			/* LOGAN status-buffer of status-port */
 long          timecnt;			/* polling timeout-counter */ 
 int           stepnbr;			/* nbr of scan/read-steps */   


 timecnt  = 0;								/* preset timeout-counter */
 pollmode = 0;					/* default: no wait for end of scan-process  */
 minor    = lax->minor;						/* get current minor-nbr */
 stepnbr  = count / 2;						/* calc scan/read-steps */

 if (stepnbr > LA_SCANBUF_MAX + 1) 			/* data-range is 0..64k-steps */
   stepnbr = LA_SCANBUF_MAX + 1;			/* limit it */

 switch (lax->mode & 0x00F0)				/* mask out address-mode */
  {case 0x00:
  			rd_adr   = 0;					/* start at baseaddress for read */
			pollmode = 0;					/* wait for end of scan-process  */
			break;	
   case 0x10:	
   			rd_adr   = lax->scan_adr; 		/* start at current read-address */
			pollmode = 0;					/* wait for end of scan-process  */
			break;
   case 0x20:	/* read from the end */ 
   			rd_adr   = (LA_SCANBUF_MAX + 1) - stepnbr; 
			pollmode = 0;					/* wait for end of scan-process */
			break;
   case 0x80:	
   			rd_adr   = 0;					/* start at baseaddress for scan */
			pollmode = 1;					/* wait for end of scan-process */
			break;	
   case 0x90:
   			rd_adr   = lax->scan_adr;		/* start at current address */
			pollmode = 1;					/* wait for end of scan-process */
			break;
   case 0xA0:	/* read from the end */ 
   			rd_adr   = (LA_SCANBUF_MAX + 1) - stepnbr; 
			pollmode = 1;					/* wait for end of scan-process */
			break;
   default:  
	  		printk("la_read*: unknown address-mode for rd = 0x%X '\n",
	  				lax->mode); 
	  		return (-1);				
  }

 scanadr = rd_adr;		    /* start reading from beginning of new data-area */

 switch (lax->mode & 0xff00)			/* select the scan & read - sources  */
  {case LA_SCAN_MASK:
		 
		 /* now start scanning with fall through to rd-buffer: 
		  * 	- set datapath to ram-port 
		  * 	- start clock and init scan-step-counter
		  * 	- free trigger-ff
		  */
		 start_scan((unsigned int)lax->mode & 0x000f, lax); 
		 
#ifdef LA_DEBUG
		 printk(" - scan is running, start polling ...");		  
#endif		  
		 status = inb(IO_STAT); 	/* read current LOGAN-status */

         /* wait for end of count of Scanning-process */
	 for(timecnt=0; (timecnt<1000000) &&
	    ((status & STAT_COUNTEND) != STAT_COUNTEND); timecnt++)
		 status = inb(IO_STAT); 	/* read current LOGAN-status */
#ifdef LA_DEBUG
         printk(" time: %X\n", (unsigned int) timecnt);
#endif         

		 if(timecnt>999999)
		  { printk("log_read: timeout in polling-mode -> no end of scan\n");
			return(-1);
		  }

	     lax->status = status; 	 /* update status-field in lax */
		 lax->scan_adr = LA_SCANBUF_MAX; /* scanprocess ready; -> end of ram */
		 pollmode = 0;			 /* polling already done */ 
		 
		 /* calc max. number of scan-data to be read */
		 if(stepnbr > ((lax->scan_adr + 1) - rd_adr))
		    count = (lax->scan_adr + 1) - rd_adr;;
		 
		 
		 
   case LA_RDBUF_MASK:
		 if(pollmode == 1)
		  { 
#ifdef LA_DEBUG
			printk(" - check if scan is running, start polling ...\n");		  
#endif		  
		    status = inb(IO_STAT); 		/* read current LOGAN-status */
 
            /* wait for end of count of Scanning-process */
	     for(timecnt=0; (timecnt<1000000) &&
	      ((status & STAT_COUNTEND) != STAT_COUNTEND); timecnt++)
		   status = inb(IO_STAT); 	/* read current LOGAN-status */
#ifdef LA_DEBUG
           printk(" time: %X\n", (unsigned int) timecnt);
#endif         

		   if(timecnt>999999)
		    { printk("log_read: timeout in polling-mode -> no end of scan\n");
			  return(-1);
   		    }

			lax->status = status;		/* member current status */ 
		  }

		 /* set scancounter to read-startaddress */
		 lax->scan_adr = rd_adr;		/* delete if function is ready */
		 pollmode = 0;			 		/* polling already done before */ 

         /* read scanbuffer to userbuffer */
		 log_reset(lax);						/* prepare read-mode */
		 rd_scandata( stepnbr, buffer, lax); 	/* read scandata */

#ifdef LA_DEBUG
         printk(" - special read-routine of la%d passed, ", minor);
	     printk("mode: %X, scanbuf-adr: %X, length: %X\n",
		        lax->mode, rd_adr, stepnbr);
#endif
         return(stepnbr * 2); 			/* return the transmitted datacount */


   case LA_RDEXT_PORT:			/* read external data-port from LOGAN here */ 
		 extdata = rd_extdata(lax);
         put_fs_byte( (unsigned char) (extdata >> 8), buffer);
		 buffer++;
		 put_fs_byte( (unsigned char) extdata, buffer);

#ifdef LA_DEBUG
         printk(" - special read-routine passed, of la%d", minor);
	     printk("   with extdata: %X,  length: 2\n", extdata);
#endif
         return(2);          		/* maximum of transmitted data is 2 */


   case LA_GETSTAT_PORT:			/* read status-port from LOGAN */ 
		 extdata = inb(IO_STAT);
		 lax->status = (unsigned char) extdata;
         put_fs_byte( (unsigned char) extdata, buffer);

#ifdef LA_DEBUG
         printk(" - special read-routine passed, of la%d", minor);
	     printk("   with status: %X\n", (unsigned char) extdata);
#endif
         return(1);          		/* maximum of transmitted data is 1 */


   case LA_GETCTRL_PORT:			/* read ctrl-port from LOGAN */ 
		 extdata = lax->ctrl;		/* get current ctrl-port-context */
         put_fs_byte( (unsigned char) extdata, buffer); /* copy to userspace */
#ifdef LA_DEBUG
         printk(" - special read-routine passed, of la%d", minor);
	     printk("    with ctrl : %X\n", (unsigned char) extdata);
#endif
         return(1);          		/* maximum of transmitted data is 1 */

   default:
		 printk("la_read*: wrong address-mode-cmd = 0x%X '\n",
				 lax->mode); 
		 return (-1);				
  }
}


/*****************************************************************************/
/* SR-Name: logan_write(struct la_struct * lax, struct file * filp,          */
/*                    unsigned char * buffer, unsigned int count)            */
/* Common :    for LOGAN-part not accessible !                               */
/*                                                                           */
/* used SR  : printk() special kernel print-routine; video-direct-access     */
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : lax    current parameterfield of subdevice                     */
/*            filp   current file-struct of device-access                    */
/*            buffer user-bufferspace for scan-data, size = count bytes !    */
/*            count  size of user-buffer in Byte                             */
/* -OUT     :  -                                                             */
/* -RETURN  : errno  negativ = error; =0  scandata read without problems     */
/*                                    =-1 something goes wrong               */
/* Variables                                                                 */
/* -external:  -                                                             */
/* -global  :  -                                                             */
/* -local   : minor  subdevice-nbr of LOGAN-device (1..256)                  */
/*                                                                           */
/*****************************************************************************/
 int logan_write(struct la_struct * lax, struct file * filp,
					   unsigned  char * buffer, unsigned int count)                
{
 int minor;

    minor = MINOR(filp->f_rdev);

#ifdef LA_DEBUG
    printk(" - special write-routine passed,");
    printk(" nothing to do for write() of la%d\n", minor);
#endif

    /* write something here, but not supported yet */ 

	return 0;  /* not yet accessible */
}


/*****************************************************************************/
/* SR-Name: logan_open(struct la_struct * lax, struct file * filp)           */
/* Common :    init LOGAN-HW for triggeraccess and set clock ready-free      */
/*                                                                           */
/* used SR  : printk() special kernel print-routine; video-direct-access     */
/*            log_initial() initializes LOGAN-hw for being quite -> sleepmode*/
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : lax    current parameterfield of subdevice                     */
/*            filp   current file-struct of device-access                    */
/* -OUT     :  -                                                             */
/* -RETURN  : errno  negativ = error; =0  LOGAN-HW starts working            */
/*                                    =-1 something goes wrong               */
/* Variables                                                                 */
/* -external:  -                                                             */
/* -global  :  -                                                             */
/* -local   : minor  subdevice-nbr of LOGAN-device (1..256)                  */
/*                                                                           */
/*****************************************************************************/
int logan_open(struct la_struct * lax, struct file * filp)
{
 int minor;

    minor = MINOR(filp->f_rdev);

#ifdef LA_DEBUG
    printk(" - special open-routine passed, of la%d\n", minor);
#endif

	log_initial(lax);		/* init LOGAN-HW: 'sleep-mode' */

	return 0;				/* and everybody is happy */

	/* HW is ready now and in sleep-mode */
}


/*****************************************************************************/
/* SR-Name: logan_close(struct la_struct * lax, struct file * filp)          */
/* Common :    reset LOGAN-hw to be quit                                     */
/*                                                                           */
/* used SR  : printk() special kernel print-routine; video-direct-access     */
/*            log_initial() initializes LOGAN-hw for being quite -> sleepmode*/
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : lax    current parameterfield of subdevice                     */
/*            filp   current file-struct of device-access                    */
/* -OUT     :  -                                                             */
/* -RETURN  :  -                                                             */
/*                                                                           */
/* Variables                                                                 */
/* -external:  -                                                             */
/* -global  :  -                                                             */
/* -local   : minor  subdevice-nbr of LOGAN-device (1..256)                  */
/*                                                                           */
/*****************************************************************************/
void logan_close(struct la_struct * lax, struct file * filp)
{
 int minor;

    minor = MINOR(filp->f_rdev);

	log_initial(lax);		/* init LOGAN-HW: 'sleep-mode' */


#ifdef LA_DEBUG
    printk(" - special close-routine passed, of la%d\n", minor);
#endif

}


/*****************************************************************************/
/* SR-Name: logan_ioctl(struct la_struct * lax, struct file * filp,          */
/*						unsigned int cmd, unsigned long arg)                 */
/* Common :    to handle dirctly the control-port of the LOGAN-HW-Part       */
/*                                                                           */
/* used SR  : printk() special kernel print-routine; video-direct-access     */
/*            log_initial() initializes LOGAN-hw for being quite -> sleepmode*/
/*            outb() write 1 byte to pc-port                                 */
/*            log_reset() set LOGAN-hw into read-mode                        */
/*                                                                           */
/*                                                                           */
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : lax    current parameterfield of subdevice                     */
/*            filp   current file-struct of device-access                    */
/* -OUT     :  -                                                             */
/* -RETURN  :  -                                                             */
/*                                                                           */
/* Variables                                                                 */
/* -external:  -                                                             */
/* -global  :  -                                                             */
/* -local   : minor  subdevice-nbr of LOGAN-device (1..256)                  */
/*                                                                           */
/*****************************************************************************/
int logan_ioctl(struct la_struct * lax, struct file * filp, 
					   unsigned int cmd, unsigned long arg)
{
 int minor;

    minor = MINOR(filp->f_rdev);	/* get current minor-nbr */

	switch(cmd)						/* select ioctl-command */
	 {case LA_IO_PUTCTRL:			/* access LOGAN-CTRL-port directly */
			outb((unsigned char) arg, IO_CTRL);
			lax->ctrl = (unsigned char) arg; /* member current ctrl-state */
	 		break;

	  case LA_IO_GETSTAT:
			lax->mode = LA_GETSTAT_PORT;  /* set: 'read status-port'-mode */
	  		break;

	  case LA_IO_PUTMODE:				/* set access-mode given by user */
	  		lax->mode = (int) arg;		/* member mode in param-field */
	  		break;

	  case LA_IO_RDCTRL:				
			lax->mode = LA_GETCTRL_PORT;  /* set: 'read ctrl-port'-mode */
	  		break;

	  case LA_IO_RESET:
			log_reset(lax); 		/* set LOGAN-Card: ready to read */
	  		break;					/* = stop clock, set address */

	  case LA_IO_INIT:				/* set LOGAN-Card: into sleep-mode */
			log_initial(lax);
	  		break;

	  default:						/* wrong command  specified by user */
#ifdef LA_DEBUG
            printk("la_ioctl*: unknown command (cmd=%d arg=%X), for la%d\n",
                    cmd, (unsigned int)arg, minor);
#endif
			return -EINVAL;
	 } 				
	  		
#ifdef LA_DEBUG
    printk(" - special ioctl-routine passed, of la%d\n", minor);
#endif

	return 0;        /* o.k. that's funny */
}



/*****************************************************************************/
/* SR-Name: log_initial(struct la_struct * lax)               22.06.1994     */
/* Usage:   INITIAL defines the Controlregister into the modes:              */
/*              - Set Data-Link to external Dataport                         */
/*              - Lock Trigger-Flag to '0'                                   */
/*              - Set Count-Address-Latch to (0000h)                         */
/*              - return-status: oscillator stopped, trigger locked ('1')    */
/*              --> LOGAN-Card is in sleep-mode                              */
/*                                                                           */
/* used SR  : inb()  read from IO-Port of AT-Bus                             */
/*            outb() write to IO-Port of AT-Bus                              */
/* Parameter                                                                 */
/* -IN      : lax       LOGAN-parameterfield of current subdevice            */
/* -OUT     : none                                                           */
/*                                                                           */
/* Variables                                                                 */
/* -external: lax->scan_adr current address-counter of scanbuffer in LOGAN-hw*/
/*            lax->status   current status of LOGAN-hw                       */
/*            lax->ctrl     current context of ctrl-port og LOGAN-hw         */
/* -global  : none                                                           */
/* -local   : inidat    current io-portdata from/in in/outport()             */
/*                                                                           */
/*****************************************************************************/

static void log_initial(struct la_struct * lax)
{
unsigned char  inidat ;                 /* wr-data-buffer             */

  inidat = 0 |  CTRL_CLRTRIG            /* init-data step 1:          */
             & ~CTRL_SETTRIG            /*   - set trigger-FF         */
             & ~CTRL_INHCLOCK           /*   - start clock-oscillator */
             |  CTRL_INHWRITE           /*   - set datapath to extern */
             |  CTRL_LOADADR            /*   - clear counter-output   */
             & ~CTRL_INCADR
             & ~CTRL_CLRADR
             |  CTRL_DATSEL ;
  outb(inidat,IO_CTRL);                 /* write to selected port     */

  inidat = 0;                           /* init-data step 2: clr-addr */
  outb(inidat,IO_ADRLO);                /* write to selected port     */
  outb(inidat,IO_ADRHI);                /* write to selected port     */

  lax->scan_adr = (unsigned int)inidat; /* member current counter-state */

  					                    /* init-data step 3:         */
  inidat = 0 |  CTRL_CLRTRIG            /*   - lock trigger-FF to 1  */
             & ~CTRL_SETTRIG            /*   - stop clock-oscilator  */
             |  CTRL_INHCLOCK           /*   - inhibit write-mode    */
             |  CTRL_INHWRITE           /*   - datapath to ext. port */
             |  CTRL_LOADADR
             & ~CTRL_INCADR
             |  CTRL_CLRADR
             |  CTRL_DATSEL ;
  outb(inidat,IO_CTRL);                 /* write to selected port    */

  lax->ctrl = inidat;					/* member curr. ctrl-state   */

  inidat = inb(IO_ADRLO);               /* init-data step 4:         */
                                        /*   - Dummy read to clear   */
                                        /*     Count-End-FF          */
  lax->status   = (unsigned char) inb(IO_STAT); /* get last status */

}   /* End of log_initial */





/*****************************************************************************/
/* SR-Name: start_scan(int ScanMode, struct la_struct * lax)	   1.08.1994 */
/* Usage:   start_scan set Counter-Start to lax->scan_adr.		             */
/*          The Scan-data will finally read into the highest word-area of    */
/*          Scan-Cache-Ram.                                                  */
/*          LOGAN-Card will be set into Scan-Mode:                           */
/*              - Set Counter-State to lax->scan_adr	                     */
/*              - Set Scan-Mode-register: 0 First Clock-frequence (50MHz)    */
/*                                          (Standard Fast-Mode (default)    */
/*                                        1 Scnd. Clock-frequence (4 MHz)    */
/*                                        2 Scnd. Clock-frequ./ 2 (2 MHz)    */
/*                                        3 Scnd. Clock-frequ./ 4 (1 MHz)    */
/*                                        4 Scnd. Clock-frequ./ 8 (512 kHz)  */
/*              - Clear Trigger-Flag and set Trigger free                    */
/*              - Return-status: oscillator started, trigger free '0'        */
/*              --> LOGAN-Card is in scan-mode                               */
/*                                                                           */
/* used SR  : inb()     read from IO-Port of AT-Bus                          */
/*            outb()    write to IO-Port of AT-Bus                           */
/* Parameter                                                                 */
/* -IN      : Scan-Mode =0 First Clock-frequence (50MHz)                     */
/*                        (Standard Fast-Mode (default)                      */
/*                      =1 Scnd. Clock-frequence (4 MHz)                     */
/*                      =2 Scnd. Clock-frequ./ 2 (2 MHz)                     */
/*                      =3 Scnd. Clock-frequ./ 4 (1 MHz)                     */
/*                      =4 Scnd. Clock-frequ./ 8 (512 kHz)                   */
/*			  lax		current LOGAN-parameterfield of subdevice			 */
/* -OUT     : none    											             */
/* -RETURN  : none															 */
/*																			 */
/* Variables                                                                 */
/* -external: lax->status   current status of LOGAN-hw                       */
/*            lax->ctrl     current context of ctrl-port og LOGAN-hw         */
/* -global  : none                                                           */
/* -local   : inidat    current io-portdata from/in in/outport()             */
/*                                                                           */
/*****************************************************************************/
static void start_scan(int ScanMode, struct la_struct * lax)
{
unsigned char  inidat;                  /* wr-data-buffer for PC-port */


  inidat = (unsigned char)lax->scan_adr; /* init-data step 1:          */
  outb(inidat, IO_ADRLO);				 /* set Counter-addresslatch   */
  inidat = (unsigned char)(lax->scan_adr >> 8); /*mask higher part to lo byte*/
  outb(inidat, IO_ADRHI);

  inidat = 0 |  CTRL_CLRTRIG            /* init-data step 2:          */
             & ~CTRL_SETTRIG            /*   - lock trigger-FF to 1   */
             & ~CTRL_INHCLOCK           /*   - start clock-oscillator */
             |  CTRL_INHWRITE           /*   - set datapath to extern */
             & ~CTRL_LOADADR            /*   - load counter startvalue*/
             & ~CTRL_INCADR
             |  CTRL_CLRADR
             |  CTRL_DATSEL ;
  outb(inidat, IO_CTRL);
										/* init-data step 3:         */
  inidat = 0 & ~CTRL_CLRTRIG            /*   - lock trigger-FF to 0  */
             |  CTRL_SETTRIG            /*   - set write-mode free   */
             & ~CTRL_INHCLOCK           /*   - lock counter with     */
             & ~CTRL_INHWRITE           /*     IncAdr-line = 1       */
             |  CTRL_LOADADR
             |  CTRL_INCADR
             |  CTRL_CLRADR
             |  CTRL_DATSEL ;
  outb(inidat, IO_CTRL);

  inidat = inb(IO_ADRLO);           	/* init-data step 4:         */
                                        /*   - Dummy read to clear   */
                                        /*     Count-End-FF          */
 						                /* init-data step 5:         */
  inidat = 0 |  CTRL_CLRTRIG            /*   - free trigger-FF to 0  */
             |  CTRL_SETTRIG            /*   - free counter with     */
             & ~CTRL_INHCLOCK           /*     IncAdr-line = 0       */
             & ~CTRL_INHWRITE           /* --> ready to scan data    */
             |  CTRL_LOADADR
             & ~CTRL_INCADR
             |  CTRL_CLRADR
             |  CTRL_DATSEL ;
  outb(inidat, IO_CTRL);

  lax->ctrl     = inidat; 			/* member last ctrl-action */
  lax->status   = (unsigned char) inb(IO_STAT);

}   /* End of start_scan() */

 




/*****************************************************************************/
/* UP-Name: rd_extdata(struct la_struct * lax)                   01.08.1994  */
/* Usage:   rd_extdata Read a 16-Bit External-port-Data from the LOGAN-Card  */
/*                                                                           */
/* used SR  : inb()  read from IO-Port of AT-Bus                             */
/*            outb() write to IO-Port of AT-Bus                              */
/* Parameter                                                                 */
/* -IN      : lax       current parameterfield of LOGAN-device               */
/* -OUT		: none															 */
/* -return  : ExtData   data-io-port-buffer						             */
/*																			 */
/* Variables                                                                 */
/* -external: lax->status   current status of LOGAN-hw                       */
/*            lax->ctrl     current context of ctrl-port og LOGAN-hw         */
/* -global  : none                                                           */
/* -local   : ctrldat   wr-io-port databuffer  for outport()                 */
/*            DataHi     rd-data-port-buffer higher for inb()                */
/*            DataLo     rd-data-port-buffer lower for inb()                 */
/*                                                                           */
/*****************************************************************************/
static unsigned int rd_extdata(struct la_struct * lax)
{
unsigned char ctrldat;                  /*  wr-data-buffer           */
unsigned int  DataHi, DataLo;				/*  buffer for each ext.-port*/

     				                    /* init-step 1:              */
  ctrldat = 0 & ~CTRL_CLRTRIG           /*  - stop-clock             */
              |  CTRL_SETTRIG           /*  - dataselect to ext.port */
              |  CTRL_INHCLOCK          /*  - lock trigger to 0      */
              |  CTRL_INHWRITE          /*  - inhibit write-mode     */
              |  CTRL_LOADADR
              & ~CTRL_INCADR
              |  CTRL_CLRADR
              |  CTRL_DATSEL ;
  outb(ctrldat, IO_CTRL);

  ctrldat = 0 |  CTRL_CLRTRIG           /* init-step 3:              */
              & ~CTRL_SETTRIG           /*   - set trigger to 1 to   */
              |  CTRL_INHCLOCK          /*     update ext. datalatch */
              |  CTRL_INHWRITE
              |  CTRL_LOADADR
              & ~CTRL_INCADR
              |  CTRL_CLRADR
              |  CTRL_DATSEL ;
  outb(ctrldat, IO_CTRL);

  DataHi  = inb(IO_DATHI);           	/* read higher dataport      */
  DataLo  = inb(IO_DATLO);         		/* read lower dataport       */

  lax->ctrl     = ctrldat;				/* member last ctrl-action */
  lax->status   = (unsigned char) inb(IO_STAT); /* get last status */

  return( (DataHi * 256) + DataLo);		/* return 16 Bit extdata-word */
  
  }   /* End of rd_extdata() */









/*****************************************************************************/
/* UP-Name: log_reset( struct la_struct * lax )						1.8.94   */
/* Usage:   log_reset  reset the LOGAN-HW-state to 'ready to read': 		 */
/*            - stop clock, preset counter-startaddress from lax->scan_adr,  */
/*              inc-adr-line to 0, lock trigger to 1                         */
/*                                                                           */
/* used SR  : inb()  read from IO-Port of AT-Bus                             */
/*            outb() write to IO-Port of AT-Bus                              */
/* Parameter                                                                 */
/* -IN      : lax		current parameterfield of LOGAN-HW					 */
/* -OUT     : none                                    						 */
/*                                                                           */
/* Variables                                                                 */
/* -external: lax->status   current status of LOGAN-hw                       */
/*            lax->ctrl     current context of ctrl-port og LOGAN-hw         */
/* -global  : none                                                           */
/* -local   : portdat    cur Portdata for RD/WR-Access to LOGAN-Ports        */
/*                                                                           */
/*****************************************************************************/
static void log_reset( struct la_struct * lax )
{
unsigned char portdat;                  /* cur. portdata         */


  portdat = 0|  CTRL_CLRTRIG				/*   - stop clock-oscillator */
             & ~CTRL_SETTRIG				/*   - inhibit write-mode    */
             |  CTRL_INHCLOCK				/*   - lock trigger-flag to 1*/
             |  CTRL_INHWRITE
             |  CTRL_LOADADR
             & ~CTRL_INCADR
             |  CTRL_CLRADR
             |  CTRL_DATSEL ;
  outb(portdat, IO_CTRL);
 
  portdat = 0|  CTRL_CLRTRIG				/* set IncAdr-line to 0      */
             & ~CTRL_SETTRIG				/* and get data from ram     */
             |  CTRL_INHCLOCK				/* set loadaddress-line to 1 */
             |  CTRL_INHWRITE				/* lock trigger-FF to 1      */
             & ~CTRL_LOADADR
             & ~CTRL_INCADR
             |  CTRL_CLRADR
             & ~CTRL_DATSEL;
  outb(portdat, IO_CTRL);

  portdat = 0|  CTRL_CLRTRIG				/* set IncAdr-line to 0      */
             & ~CTRL_SETTRIG				/* and get data from ram     */
             |  CTRL_INHCLOCK				/* set loadaddress-line to 1 */
             |  CTRL_INHWRITE
             & ~CTRL_LOADADR
             & ~CTRL_INCADR
             |  CTRL_CLRADR
             & ~CTRL_DATSEL;
  outb(portdat, IO_CTRL);

  outb(lax->scan_adr & 0x00ff, IO_ADRLO);		/* set Counteraddress-port   */
  outb(lax->scan_adr >> 8,     IO_ADRHI);

  /* dummy-read to lower addressport for reset Countend-FF   */
  portdat = inb(IO_ADRLO);				

  portdat = 0|  CTRL_CLRTRIG				/* set IncAdr-line to 1      */
             & ~CTRL_SETTRIG
             |  CTRL_INHCLOCK
             |  CTRL_INHWRITE
             & ~CTRL_LOADADR
             |  CTRL_INCADR
             |  CTRL_CLRADR
             & ~CTRL_DATSEL ;
  outb(portdat, IO_CTRL);
  
  portdat = 0|  CTRL_CLRTRIG				/* set IncAdr-line to 0      */
             & ~CTRL_SETTRIG				/* and LoadAdr to 1          */
             |  CTRL_INHCLOCK
             |  CTRL_INHWRITE
             |  CTRL_LOADADR
             & ~CTRL_INCADR
             |  CTRL_CLRADR
             & ~CTRL_DATSEL ;
  outb(portdat, IO_CTRL);

lax->ctrl     = portdat;						/* member last ctrl-action */
lax->status   = (unsigned char) inb(IO_STAT);	/* member last status */

} /* end of log_reset() */


/*****************************************************************************/
/* UP-Name: rd_scandata(unsigned int StepNbr, unsigned char *ScanBuf  1.8.94 */
/*						struct la_struct * lax )							 */
/* Usage:   rd_scandata reads 'StepNbr'-words of 16 Bit into ScanBuf		 */
/*          beginning with address lax->scanadr.                             */
/*                                                                           */
/* used SR  : inb()  read from IO-Port of AT-Bus                             */
/*            outb() write to IO-Port of AT-Bus                              */
/*            put_fs_byte() copy bytewise data to userspace                  */
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : StepNbr   Number of 16-Bit Scan-Data in Scan-Cache-ram 0..64k  */
/*            lax		current parameterfield of LOGAN-HW					 */
/* -IN/OUT  : ScanBuf   DataBuffer, range(min) = StepNbr...0xFFFF  			 */
/* -OUT     : return    =0	everything o.k. (default)						 */
/*                                                                           */
/* Variables                                                                 */
/* -external: lax->scan_adr current address-counter of scanbuffer in LOGAN-hw*/
/*            lax->status   current status of LOGAN-hw                       */
/*            lax->ctrl     current context of ctrl-port og LOGAN-hw         */
/* -global  : none                                                           */
/* -local   : portdat    cur Portdata for RD/WR-Access to LOGAN-Ports        */
/*            bufaddress current address of Scan-Cache-Ram 0..64k            */
/*            bufend     endaddress of Scan-Cache-Ram-Page                   */
/*                                                                           */
/*****************************************************************************/
static unsigned int rd_scandata( unsigned int StepNbr, unsigned char *ScanBuf,
		  						 struct la_struct * lax )
{
unsigned char portdat;                  /* cur. portdata         */
unsigned int  bufaddress;               /* Scan-Ram addressindex */
unsigned int  bufend;                   /* Scan-Ram endaddress   */


  portdat = 0;
  bufaddress = lax->scan_adr; 				/* get scanbuffer startaddress */
  bufend  = bufaddress + StepNbr;			/* define endaddress of scanbuf*/

  for(; bufaddress <= bufend; bufaddress++)
   { /* fill user-buffer */
   
     put_fs_byte( (unsigned char) inb(IO_DATLO), ScanBuf++);
     put_fs_byte( (unsigned char) inb(IO_DATHI), ScanBuf++);

     portdat = 0 |  CTRL_CLRTRIG			/* set IncAdr-line to 1      */
                 & ~CTRL_SETTRIG
                 |  CTRL_INHCLOCK
                 |  CTRL_INHWRITE
                 |  CTRL_LOADADR
                 |  CTRL_INCADR
                 |  CTRL_CLRADR
                 & ~CTRL_DATSEL ;
     outb(portdat,IO_CTRL);					/* write to selected port    */

     portdat = 0|  CTRL_CLRTRIG             /* set IncAdr-line to 1      */
                & ~CTRL_SETTRIG
                |  CTRL_INHCLOCK
                |  CTRL_INHWRITE
                |  CTRL_LOADADR
                & ~CTRL_INCADR
                |  CTRL_CLRADR
                & ~CTRL_DATSEL ;
     outb(portdat, IO_CTRL);				/* write to selected port    */
   };

lax->scan_adr = bufaddress; 		/* member last counter-address*/
lax->ctrl     = portdat;			/* member last ctrl-action */
lax->usr_adr  = (unsigned int) ScanBuf;	/* member last userbuffer-address */

return(0);
}   /* End of rd_scandata */
