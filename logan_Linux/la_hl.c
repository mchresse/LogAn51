
/*
 *  linux/drivers/char/la_hl.c
 *
 *  Copyright (C) 1994, Randolph Esser        
 */
/*****************************************************************************
**
** Dev.-Name: LA                                                 12.07.1994  
** Common:                                                                   
** 'la_hl.c' makes the opt. logic-analyser LOGAN16 transparent for initia-
** lization the scanning HW, start scanning by external trigger and 
** reading the actually filled scan-buffer.
** The whole logic-analyser-card is selfmade. So you can get the plans
** of the HW by sending a paid letter to:
**                       Esser Randolph
**						 Albrecht-Duererstr.13
**                       85579 Neubiberg 
**                       Germany
**
** or you look at the framemaster-file logan.doc.
** Thanks to the authors of the tty-device-driver. I lent their sub-device-
** structure for optional 'goody-devices' in former times on 
** the same card (perhaps a 12bit-AD/DA-Sampler-extension with 1 MHz-clock).
** They are now dynamically allocated only when the la-sub-device is opened.
**
** I used the same clean structure of high-level (la_hl.c) and low-level 
** (la_ll.c) routines.                   
**                                  
** competent files for LOGAN-Card:
**		- /linux/drivers/char/la_hl.c		high.level kernel-satisfactions
**      - /linux/drivers/char/la_ll.c		lowlevel hw-accesses
**											(the real driver-src )
**		- /linux/include/linux/la.h			global prototyping and includes
**		- /linux/include/linux/la_def.h		global defines for LOGAN-device
**											(usefull for user-prg-include)
**
** updated files for LOGAN-connection into kernel-access (LINUX 1.0):
**		- /linux/drivers/char/mem.c			chain la_init() into chr_dev_init()
**											and config-conditioned prototyping  
**											at the beginning for la_init()
**											(L.434+3 and 25+3)
**		- /linux/drivers/char/Makefile      add la_hl.c and la_ll.c to char.a-
**											source-definition (L.51+4) 
**		- /linux/config.in					add CONFIG_LOGAN_CARD-definition to
**											char-driver-configuration (L.123+4)
**											(optional is CONFIG_LOGAN_ADDA-def)
**		- /linux/include/linux/major.h		add LA_MAJOR to Major-Nbr-list
**											(L.48+2 and 78+1)
**
** The filenode for 1. LOGAN-device is initiated with: 'mknod /dev/la1 c 31 1'.
**
** With defined LA_DEBUG in la_def.h you can get a lot of passing-messages for
** each device-command after recompiling the kernel (Give me an 'Uuurgh'...).
**
** (Remark: ignore any warnings of la_ll.c cause of paranthesis, -> it's o.k.) 
**                                                                             
** Randolph Esser   7/12/94                                                
**                                                                             
******************************************************************************
*/

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
#include <linux/kernel.h>

#include <linux/la.h>			/* inclusive la_def.h */


/****************************************************************************/
/* definitions */

#define MAX_OPEN_RETRY 2 /* Max. number of accepted interrupts while open() */
                                                                   
#define MAX_SUBDEV 256   /* Max. number of minor's to LOGAN-Card  */
#define MAX_LAS    1     /* Max. number of connected LOGAN-Cards  */

struct la_struct * la_table[MAX_SUBDEV]; /* pointerarray of initialized LAs */


/****************************************************************************/
/* prototyping and declarations:: */

       long la_init(long);
static long init_logan(long);
static long init_adda(long);

static int  la_open(struct inode *, struct file *);
static int  init_dev(int);
static void init_logan_struct(int, struct la_struct *);

static void la_release(struct inode *, struct file *);
static void release_dev(int, struct file *);

static int  la_lseek(struct inode *, struct file *, off_t, int);
static int  la_read(struct inode *, struct file *, char *, int);
static int  la_write(struct inode *, struct file *, char *, int);

static int  la_select(struct inode *, struct file *, int, select_table *);
static int  la_ioctl(struct inode *, struct file *,unsigned int,unsigned long);



static struct file_operations la_fops = {
	la_lseek,
	la_read,
	la_write,
	NULL,		/* la_readdir, there is no directory to manage */
	la_select,
	la_ioctl,
	NULL,		/* la_mmap, now there is nothing to mmap */
	la_open,
	la_release
};




/*****************************************************************************/
/* local functions: */

/*****************************************************************************/
/* SR-Name: long la_init(long kmem_start)                                    */
/* Common : hook LOGAN/ADDA-device into init-chain of kernel-boot-process.   */
/*          All special device-functions are defined in la_fops-structure.   */
/*          They are accessible from now on until shutdown.                  */
/*                                                                           */
/* used SR  :  memset() reserves system-memory for char-driver               */
/*             panic()  performs a 'clean crash' cause of kernel-error       */
/*             printk() kernel print-routine with video-direct-access        */
/*             sizeof() calc size of data-structere                          */
/*             register_chrdev() hook current device into init-chain         */
/*             init_logan() reserves needed rampages as resident area >LOGAN */
/*             init_adda() reserves needed rampages as resident area >AD/DA  */
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : kmem_start  ptr. to last free address of system-memory         */
/* -OUT     :  -                                                             */
/* -RETURN  : kmem_start  new pointer to last free address of system-memory  */
/*                                                                           */
/* Variables                                                                 */
/* -external:  -                                                             */
/* -global  : la_table[] vector of opened subdevice vectorfields             */
/* -local   : i          index of la_table[] = ment minor-number             */
/*                                                                           */
/*****************************************************************************/
long la_init(long kmem_start)
{
	int i;		/* index of la_table[] for vector-init */


    printk("la_init: start init of Logan-Card, Major: %d\n", LA_MAJOR);

#ifdef LA_DEBUG
    printk("         kmem_start before la_init: %X \n",
          (unsigned int) kmem_start);             
#endif  
  
    /* check max. static memsize */
  	if (sizeof(struct la_struct) > PAGE_SIZE) 
  		panic("la_init: size of la-structure > PAGE_SIZE!");
 
    /* link for kernel-knowledge of this device */
	if (register_chrdev(LA_MAJOR,"la",&la_fops))
		panic("la_init: unable to get major %d for la-device", LA_MAJOR);

	/* reset vector-table: la_table[] in front of any open()-call */
	for (i=0 ; i< MAX_SUBDEV ; i++)
	    la_table[i] = 0;
	    
    /* check mem-requests for LOGAN-subdevices */
	kmem_start = init_logan(kmem_start);
	kmem_start = init_adda(kmem_start);

#ifdef LA_DEBUG
    printk("         kmem_start after  la_init: %X \n",
           (unsigned int) kmem_start);             
#endif  

	return kmem_start; /* handle new end-of-mem back to the kernel */
}


/*****************************************************************************/
/* SR-Name: init_logan(long kmem_start)                                      */
/* Common : invokes usement of special kernel-memory for LOGAN-Card-handling */
/*                                                                           */
/* used SR  :  -                                                             */
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : kmem_start  ptr. to last free address of system-memory         */
/* -OUT     :  -                                                             */
/* -RETURN  : kmem_start  new pointer to last free address of system-memory  */
/*                                                                           */
/* Variables                                                                 */
/* -external:  -                                                             */
/* -global  :  -                                                             */
/* -local   :  -                                                             */
/*                                                                           */
/*****************************************************************************/
static long init_logan(long kmem_start)
{
 /* at this time no mem-request, but might be possible in the future */

 return kmem_start;      /* handle new end-of-mem back to the kernel */ 
}



/*****************************************************************************/
/* SR-Name: init_adda(long kmem_start)                                       */
/* Common : invokes usement of special kernel-memory for LOGAN-Card-handling */
/*          especialy for the AD/DA-Sampling-functionality                   */
/* used SR  :  -                                                             */
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : kmem_start  ptr. to last free address of system-memory         */
/* -OUT     :  -                                                             */
/* -RETURN  : kmem_start  new pointer to last free address of system-memory  */
/*                                                                           */
/* Variables                                                                 */
/* -external:  -                                                             */
/* -global  :  -                                                             */
/* -local   :  -                                                             */
/*                                                                           */
/*****************************************************************************/
static long init_adda(long kmem_start)
{
 /* at this time no mem-request, but might be possible in the future */

 return kmem_start;      /* handle new end-of-mem back to the kernel */ 
}

/*****************************************************************************/
/* SR-Name: la_open(struct inode * inode, struct file * filp)                */
/* Common : allows only 1 open per la-device. The new-generated la-structure-*/
/*          pointer is written to the next free entry in la_table[].         */
/*                                                                           */
/* used SR  : MINOR() calc minor-nbr out of device-nbr                       */
/*            MAJOR() calc major-number out of device-nbr                    */
/*            printk() kernel print-routine with video-direct-access         */
/*            release_dev() frees current device like close-process and un-  */
/*                          chains the parameterfield from la_table[]        */
/*            schedule() assign new process from waitqueue for action        */
/*            MKDEV()  calc device-nbr from given major and minor-nbr        */
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : inode  current inode-struct of device-access                   */
/*            filp   current file-struct of device-access                    */
/* -OUT     :  -                                                             */
/* -RETURN  : errno  negativ = error; =0 open was allowed                    */
/*                                                                           */
/* Variables                                                                 */
/* -external: current    structure of current process-parameters             */
/* -global  : la_table[] vector of opened subdevice vectorfields             */
/* -local   : lax        ptr to currently used subdevice-parameterfield      */
/*            major      current major-nbr of this device -> LOGAN-Card      */
/*            minor      current minor-nbr of this subdevice -> logan,ad/da  */
/*            retval     buffer of connected returncode                      */
/*            retry      count nbr of retries for opening (0..MAX_OPEN_RETRY)*/
/*                                                                           */
/*****************************************************************************/
static int la_open(struct inode * inode, struct file * filp)
{
 struct la_struct *lax;       /* parameterfield-ptr of subdevices */ 
 int major, minor;            /* parameters of device-nbr */
 int retval;                  /* returncode of open() */
 int retry;                   /* open()-retry-counter (0...MAX_OPEN_RETRY) */


	retry = 0;                    /* preset: first open()-call */
	minor = MINOR(inode->i_rdev); /* get current minor-nbr */
	major = MAJOR(inode->i_rdev); /* get current major-nbr */

#ifdef LA_DEBUG
    printk("la_open: try to open device: Major= %d, Minor= %d\n", major,minor);    
#endif

retry_open:						/* entry for each open-retry if any trouble */

	if (major != LA_MAJOR)       /* is it the right device for open ? */ 
	 {                           /* wrong major-nbr detected */
	   printk("la_open: Bad major #%d \n", major);
       return -ENODEV;           /* break with error */
	 }
   
    /* wrong minor-nbr defined ? */ 	
	if ((minor < LA_MINOR_LOGAN1) | (minor > LA_MINOR_ADDA4))
     { printk("la_open: Bad minor #%d, out of range\n", minor);
       return -ENXIO;            /* break with error */
     } 

	filp->f_rdev = (major << 8) | minor; /* redefine device-nbr in file */
    
    retval = init_dev(minor);   /* init la_struct and chain it to la_table[]*/

	if ( retval != 0)			/* if any error in init la_struct[] */
	 {
#ifdef LA_DEBUG
        printk(" - init_dev returns with error #%d \n", retval);
#endif
	    return retval;           /* error detected, break with error */
	 }

	lax = la_table[minor];       /* get chained parameterfield-ptr of subdev */

#ifdef LA_DEBUG
   printk(" - opening la%d; struct-ptr: %X \n", lax->minor,(unsigned int)lax);
#endif

	if (lax->open > 0) 			 /* if subdevice not already opened */
	 { retval = lax->open(lax, filp); /*special open of subdevice on lowlevel*/
	 } 
	else 
	 { 
#ifdef LA_DEBUG
	   printk(" - no special open-routine defined \n");
#endif
       retval = 0;
	 }
	
	if (retval != 0) 			 /* if there was any trouble with signals */
     {							 /* uuurgh ... o.k. again */

#ifdef LA_DEBUG
		printk(" - error #%d in special opening of la%d\n", retval,lax->minor);
#endif
		release_dev(minor, filp); /* give up this subdevice: can't open */

        if (current->signal & ~current->blocked) /* is a ext. signal detected*/
			return retval;        /* break with normal-error */                   
		schedule();               /* satisfy current signal */

#ifdef LA_DEBUG
    	printk(" - retry opening la%d cause of extern-signal\n", minor);
#endif
        retry++;                   /* member the number of retries */
        if (retry <= MAX_OPEN_RETRY) /* retry allowed yet */
           goto retry_open;        /* ext. error, get a 2. chance to open() */
       
        printk("la_open: can't open device %X\n", MKDEV(major,minor)); 
	    filp->f_rdev = MKDEV(LA_MAJOR, 1);  /* Set it to something normal */
	    return -ENODEV;        /* open unsuccessfull, to many retries needed */
	 }

	return 0;					   /* everything is o.k., open() successfull */
}


/*****************************************************************************/
/* SR-Name: init_dev(int minor)                                              */
/* Common : initializes a LOGAN-device specified by its minor-nbr            */
/*                                                                           */
/* used SR  : printk() kernel print-routine with video-direct-access         */
/*            free_page() free neede ram-page if any error while open-process*/
/*            init_logan_struct() initialize la_struct with default-values   */
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : minor  minor-number of current device                          */
/* -OUT     :  -                                                             */
/* -RETURN  : error  =0 o.k. else -> errno.h                                 */
/*                                                                           */
/* Variables                                                                 */
/* -extern  :  -         													 */
/* -global  : la_table[] vector of opened subdevice vectorfields             */
/* -local   : lax        ptr to currently used subdevice-parameterfield      */
/*                                                                           */
/*****************************************************************************/
static int init_dev(int minor)
{
 struct la_struct *lax;


    lax = NULL;               /* preset new parameterfield-ptr */
   
    /* end if subdevice is already opened and initialized */ 
	if (la_table[minor] != NULL)    
	 {  if (la_table[minor]->open_cnt == 1)
		{ printk("la_open: device is already opened of la%d \n", minor);
          return -EAGAIN;    /* return with errorcode: too many open */
	    }
	   else
 		{ printk("la_open: wrong open-count handling: #%d of la%d\n",
 		          la_table[minor]->open_cnt, minor);
	      return -EMFILE;   /* return with errorcode: wrong open-cnt handling*/
        } 
	  }
	  
	lax = (struct la_struct*) get_free_page(GFP_KERNEL);

    if (lax == NULL)
     { /* free memory if open() was errorneous */
   	   free_page((unsigned long) lax);
       return -ENOMEM;
	 }

    init_logan_struct(minor, lax); /* init this new parameter-field */


/* Now we have allocated all the structures: updated all the pointers.. */

	/*chain new la-structure to dev-table: la_table[] */
	la_table[minor] = lax; 
	lax = NULL;            		 /* free the old parameterfield-ptr */

	la_table[minor]->open_cnt++; /* mark LOGAN-Card opened */
	return 0;                    /* return with open()-o.k. */

}


/*****************************************************************************/
/* SR-Name: init_logan_struct(int minor, struct la_struct * lax)             */
/* Common :                                                                  */
/*                                                                           */
/* used SR  : memset() fill ram-block with given filldata                    */
/*            sizeof() calc size of data-struct in byte                      */
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : lax    ptr to la-struct to be initialized                      */
/*            minor  current minor-nbr of la-device                          */
/* -OUT     :  -                                                             */
/* -RETURN  :  -                                                             */
/*                                                                           */
/* Variables                                                                 */
/* -external:  -                                                             */
/* -global  :  -                                                             */
/* -local   :  -                                                             */
/*                                                                           */
/*****************************************************************************/
static void init_logan_struct(int minor, struct la_struct * lax)
{
    memset(lax, 0, sizeof(struct la_struct));  /* delet complete structure=0 */

    lax->minor    = minor;            /* set current minor-nbr of LOGAN-card */
    lax->mode     = LA_SCAN0_BASE;    /* default: read from baseadr to end   */
    lax->open     = logan_open;       /* set links to subdev-spec. functions */
    lax->close    = logan_close;
    lax->read     = logan_read;
    lax->write    = logan_write;
    lax->select   = logan_select;
    lax->ioctl    = logan_ioctl;
    lax->lseek    = logan_lseek;
}





/*****************************************************************************/
/* SR-Name: la_select(struct inode * inode, struct file * filp,              */
/*                    int sel_type, select_table * wait)                     */
/* Common : tells o.k. if the la-device is in ready-state.                   */
/*          (irq-handling was not attempt yet)                               */
/*                                                                           */
/* used SR  : MINOR() calc minor-nbr out of device-nbr                       */
/*            MAJOR() calc major-number out of device-nbr                    */
/*            printk() kernel print-routine with video-direct-access         */
/*            lax->select() lowlevel select-routine with direct hw-accesses  */
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : inode  current inode-struct of device-access                   */
/*            filp   current file-struct of device-access                    */
/*            sel_type = SEL_IN  for call: 'ready for read ?'                */
/*                     = SEL_OUT for call: 'ready for write ?'               */
/*                     = SEL_EX  for call: 'got an exception ?'              */
/*            wait   list of file-descriptors                                */
/* -OUT     :  -                                                             */
/* -RETURN  : errno  negativ = error; =0 device not ready for 'sel_type'-acc.*/
/*                                    =1 device ready for 'sel_type'-access  */
/* Variables                                                                 */
/* -external:  -                                                             */
/* -global  : la_table[] vector of opened subdevice vectorfields             */
/* -local   : lax        ptr to currently used subdevice-parameterfield      */
/*            major      current major-nbr of this device -> LOGAN-Card      */
/*            minor      current minor-nbr of this subdevice -> logan,ad/da  */
/*                                                                           */
/*****************************************************************************/
static int la_select(struct inode * inode, struct file * filp,
                     int sel_type, select_table * wait)
{
 int minor, major;
 struct la_struct * lax;

	
    major = MAJOR(filp->f_rdev);		/* get current minor & major-nbr of */
    minor = MINOR(filp->f_rdev);		/* this subdevice */
    
#ifdef LA_DEBUG
    printk("la_select: try to select device: Major= %d Minor= %d\n",
            major, minor);
#endif
   
	/* break if wrong major-nbr is detected */
	if (major != LA_MAJOR)
	 {	printk("la_select: bad pseudo-major nr #%d\n", major);
		return 0;
	 }
	 
    /* wrong minor-nbr defined ? */ 	
	if ((minor < LA_MINOR_LOGAN1) | (minor > LA_MINOR_ADDA4))
     { printk("la_select: Bad minor #%d, out of range\n", minor);
       return 0;            /* break with error */
     } 

	lax = la_table[minor];         /* get the competent parameter-field-ptr */
	
	if (lax == NULL)     		   /* if subdevice not opened */
	 {	printk("la_select: device la%d not open \n",minor );
		return 0;                  /* return: device not ready */
	 }
	 
	if(lax->select)                /* if spec. select-function chained ? */
	   	return (lax->select)(lax, filp, sel_type, wait);

#ifdef LA_DEBUG
    printk("la_select: no special lseek-routine defined of la%d\n", minor);
#endif
    return 0;                      /* else error-break: wrong lseek */
}


/*****************************************************************************/
/* SR-Name: la_lseek(struct inode * inode, struct file * file,     22.6.1994 */ 
/*				    off_t offset, int origin)                                */
/* Common : reloads the counter-value of scanbuffer in LOGAN-Card by calling */
/*          the specific logan_lseek()-function.                             */
/* used SR  : MAJOR  extract major-nbr from device-nbr                       */
/*            MINOR  ectract minor-nbr from device-nbr                       */
/*            printk() kernel print-routine with video-direct-access         */
/*            logan_lseek reload scan-buffer-address-counter on LOGAN-Card   */
/*            lax->lseek() lowlevel lseek-routine with direct hw-accesses    */
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : inode  current inode-struct of device-access                   */
/*            file   current file-struct of device-access                    */
/*            offset offset reativ to scan-buffer-base (0000h ... FFFFh)     */
/*            origin 0 = 'offset' is based on the buffer-beginning (0000h)   */
/*                   1 = 'offset' is based on the current address            */
/*                   2 = 'offset' is counted from the buffer-end (FFFFh)     */
/* -OUT     :  -                                                             */
/* -RETURN  : errno  negativ = error; >= 0 is reached absolut position       */
/*                       -> errno.h                                          */
/* Variables                                                                 */
/* -external:  -                                                             */
/* -global  : la_table[] vector of opened subdevice vectorfields             */
/* -local   : lax        ptr to currently used subdevice-parameterfield      */
/*            major      current major-nbr of this device -> LOGAN-Card      */
/*            minor      current minor-nbr of this subdevice -> logan,ad/da  */
/*                                                                           */
/*****************************************************************************/
static int la_lseek(struct inode * inode, struct file * file,
				    off_t offset, int origin)
{
	int minor, major;
	struct la_struct * lax;

    major = MAJOR(file->f_rdev);		/* get current minor & major-nbr of */
    minor = MINOR(file->f_rdev);		/* this subdevice */
    
#ifdef LA_DEBUG
    printk("la_lseek: try to lseek device: Major= %d Minor= %d\n",
            major, minor);
#endif
   
	/* break if wrong major-nbr is detected */
	if (major != LA_MAJOR)
	 {	printk("la_lseek: bad pseudo-major nr #%d\n", major);
		return -EINVAL;
	 }
	 
    /* wrong minor-nbr defined ? */ 	
	if ((minor < LA_MINOR_LOGAN1) | (minor > LA_MINOR_ADDA4))
     { printk("la_select: Bad minor #%d, out of range\n", minor);
       return -EINVAL;          /* break with error */
     } 

	lax = la_table[minor];      /* get the competent parameter-field-ptr */
	
	if (lax == NULL)     		/* if subdevice not opened */
	 {	printk("la_lseek: device la%d not open \n",minor );
		return -ENODEV; 
	 }
	 
	/* call if spec. lseek-function is chained ? */	 
	if(lax->lseek)       
	   	return ((int)(lax->lseek)(lax, file, offset, origin));

#ifdef LA_DEBUG
    printk("la_lseek: no special lseek-routine defined of la%d\n", minor);
#endif
    return -ESPIPE;                     /* error-break: wrong lseek */
}


/*****************************************************************************/
/* SR-Name: la_read(struct inode * inode, struct file * file,                */
/*                  char * buf, int count)                                   */ 
/* Common : Select current la-structure in la_table and starts the competent */
/*          routine to read the scan-buffer.                                 */
/*                                                                           */
/* used SR  : MAJOR  extract major-nbr from device-nbr                       */
/*            MINOR  ectract minor-nbr from device-nbr                       */
/*            printk() kernel print-routine with video-direct-access         */
/*            logan_read  read actual scan-buffer depending on ioctl-f|ags   */
/*                        in the inode- and lax-structure.                   */
/*            lax->read() lowlevel read-routine with direct hw-accesses      */
/* Parameter                                                                 */
/* -IN      : inode  current inode-struct of device-access                   */
/*            file   current file-struct of device-access                    */
/*            buf    base-address of user-buffer to be filled                */
/*            count  number of bytes to be read into 'buf'                   */
/* -OUT     :  -                                                             */
/* -RETURN  : errno  negativ = error; =0 'count' bytes are read              */
/*                     -> errno.h                                            */
/* Variables                                                                 */
/* -external:  -                                                             */
/* -global  : la_table[] vector of opened subdevice vectorfields             */
/* -local   : lax        ptr to currently used subdevice-parameterfield      */
/*            major      current major-nbr of this device -> LOGAN-Card      */
/*            minor      current minor-nbr of this subdevice -> logan,ad/da  */
/*                                                                           */
/*****************************************************************************/
static int la_read(struct inode * inode, struct file * file,
                   char * buf, int count)
{
	int minor, major;
	struct la_struct * lax;

    major = MAJOR(file->f_rdev);
    minor = MINOR(file->f_rdev);
    
#ifdef LA_DEBUG
    printk("la_read: try to read from device: Major= %d Minor= %d\n",
            major, minor);
#endif
   
	/* break if wrong major-nbr is detected */
	if (major != LA_MAJOR)
	 {	printk("la_read: bad pseudo-major nr #%d\n", major);
		return -EINVAL;
	 }
	 
	lax = la_table[minor];      /* get the competent parameter-field-ptr */
	
	if (lax == NULL)     		/* if subdevice not opened */
	 {	printk("la_read: device: la%d not open \n",minor );
		return -ENODEV; 
	 }

	/* call if spec. read-function is chained */	 
	if (lax->read)              
		return(lax->read)(lax,file,(unsigned char *)buf,(unsigned int)count);

#ifdef LA_DEBUG
    printk("la_read: no special read-routine defined of la%d\n", minor);  
#endif

	return -EIO;                 /* return: IO-Error */
}


/*****************************************************************************/
/* SR-Name: la_write(struct inode * inode, struct file * file,               */
/*                  char * buf, int count)                                   */ 
/* Common : Select current la-structure in la_table and starts the competent */
/*          routine to write to the la-card.                                 */
/*                                                                           */
/* used SR  : MAJOR  extract major-nbr from device-nbr                       */
/*            MINOR  ectract minor-nbr from device-nbr                       */
/*            printk() kernel print-routine with video-direct-access         */
/*            lax->write() lowlevel write-routine with direct hw-accesses    */
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : inode  current inode-struct of device-access                   */
/*            file   current file-struct of device-access                    */
/*            buf    base-address of user-buffer to be read from             */
/*            count  number of bytes to be write to la-card                  */
/* -OUT     :  -                                                             */
/* -RETURN  : errno  negativ = error; =0 'count' bytes are read              */
/*                                                                           */
/* Variables                                                                 */
/* -external:  -                                                             */
/* -global  : la_table[] vector of opened subdevice vectorfields             */
/* -local   : lax        ptr to currently used subdevice-parameterfield      */
/*            major      current major-nbr of this device -> LOGAN-Card      */
/*            minor      current minor-nbr of this subdevice -> logan,ad/da  */
/*                                                                           */
/*****************************************************************************/
static int la_write(struct inode * inode, struct file * file,
                    char * buf, int count)
{
	int minor, major;
	struct la_struct * lax;

    major = MAJOR(file->f_rdev);
    minor = MINOR(file->f_rdev);
    
#ifdef LA_DEBUG
    printk("la_write: try to write to device: Major= %d Minor= %d\n",
            major, minor);
#endif
   
	/* break if wrong major-nbr is detected */
	if (major != LA_MAJOR)
	 {	printk("la_write: bad pseudo-major nr #%d\n", major);
		return -EINVAL;
	 }
	 
	lax = la_table[minor];      /* get the competent parameter-field-ptr */
	
	if (lax == NULL)     		/* if subdevice not opened */
	 {	printk("la_write: device: la%d not open \n",minor );
		return -ENODEV; 
	 }

	/* call if spec. write-function is chained */	 
	if (lax->read)              
		return(lax->write)(lax,file,(unsigned char *)buf,(unsigned int)count);

#ifdef LA_DEBUG
    printk("la_write: no special write-routine defined of la%d\n", minor);  
#endif

	return -EIO;               /* return: IO-Error */
}


/*****************************************************************************/
/* SR-Name: la_release(struct inode * inode, struct file * filp)             */
/* Common : free the current la-device-structure from la_table and frees     */
/*          used kmems.                                                      */
/*                                                                           */
/* used SR  : MAJOR  extract major-nbr from device-nbr                       */
/*            MINOR  ectract minor-nbr from device-nbr                       */
/*            printk() kernel print-routine with video-direct-access         */
/*            release_dev() free current la_struct from la_table[]           */
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : inode  current inode-struct of device-access                   */
/*            filp   current file-struct of device-access                    */
/* -OUT     :  -                                                             */
/* -RETURN  :  -                                                             */
/*                                                                           */
/* Variables                                                                 */
/* -external:  -                                                             */
/* -global  : la_table[] vector of opened subdevice vectorfields             */
/* -local   : lax        ptr to currently used subdevice-parameterfield      */
/*            major      current major-nbr of this device -> LOGAN-Card      */
/*            minor      current minor-nbr of this subdevice -> logan,ad/da  */
/*                                                                           */
/*****************************************************************************/
static void la_release(struct inode * inode, struct file * filp)
{
 int major, minor;            /* parameters of device-nbr */


	minor = MINOR(filp->f_rdev); /* get current minor-nbr */
	major = MAJOR(filp->f_rdev); /* get current major-nbr */

#ifdef LA_DEBUG
    printk("la_release: try to close device: Major= %d, Minor= %d\n",
            major,minor);    
#endif

	if (major != LA_MAJOR)       /* is it the right device for close ? */ 
	 {                           /* wrong major-nbr detected */
	   printk("la_close: Bad major #%d \n", major);
       return;                   /* and do nothing -> end */
	 }

    /* wrong minor-nbr defined ? */ 	
	if ((minor < LA_MINOR_LOGAN1) | (minor > LA_MINOR_ADDA4))
     { printk("la_close: Bad minor #%d, out of range\n", minor);
       return;                   /* and do nothing -> end */
     }

#ifdef LA_DEBUG
    printk(" - closing device #%X ...\n", filp->f_rdev);
#endif
	
	release_dev(minor, filp);
}

/*****************************************************************************/
/* SR-Name: release_dev(int minor, struct file * filp)                       */
/* Common : free the current la-device-structure from la_table               */
/*                                                                           */
/* used SR  : printk() kernel print-routine with video-direct-access         */
/*            lax->close() lowlevel close-routine with direct hw-accesses    */
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : minor  current minor-nbr                                       */
/*            filp   current file-struct of device-access 					 */
/* -OUT     :  -                                                             */
/* -RETURN  :  -                                                             */
/*                                                                           */
/* Variables                                                                 */
/* -external:  -                                                             */
/* -global  : la_table[] vector of opened subdevice vectorfields             */
/* -local   : lax        ptr to currently used subdevice-parameterfield      */
/*                                                                           */
/*****************************************************************************/
static void release_dev(int minor, struct file * filp)
{
 struct la_struct *lax;


	lax = la_table[minor];	/* get la_struct-ptr of current subdevice */

	if (lax == NULL)		/* if subdevice not opened */
	 {	printk("la_release: error, device la%d is not open \n", minor);
		return;                  /* return: nothing to do, no device present */
	 }

#ifdef LA_DEBUG
	printk(" - releasing la%d (lax count=%d)\n",
	       minor, lax->open_cnt);
#endif
    
	if (lax->close)               /* spec. close-function chained ? */
		lax->close(lax, filp);    /* call lowlevel close of subdevice */

	if ((--lax->open_cnt) < 0)    /* impossible wrong open-count ? */
	 {	printk("la_release: bad la_table[%d]->open_cnt: %d, i correct it\n",
	            minor, lax->open_cnt);
		lax->open_cnt = 0;        /* redefine close-state */
     }

	if (lax->open_cnt)         /* if any process controls this subdevice: */ 
	 {  
#ifdef LA_DEBUG
  	printk(" - close without release ! There is another controlling process.");
#endif
	  return;                /* close o.k., return without error */
	 }
	
#ifdef LA_DEBUG
	printk(" - release device, freeing LA structure\n");
#endif

	la_table[minor] = NULL;               /* free subdev-entry for next open */
	free_page((unsigned long) lax);    /* free reserved kernel memory of lax */
}


/*****************************************************************************/
/* SR-Name: la_ioctl(struct inode * inode, struct file * file,               */
/*                   unsigned int cmd, unsigned long arg)                    */
/* Common : allows direct-access to the la-card port-register by cmd-codes.  */
/*                                                                           */
/* used SR  : printk() kernel print-routine with video-direct-access         */
/*            lax->ioctl() lowlevel ioctl-routine with direct hw-accesses    */
/*                                                                           */
/* Parameter                                                                 */
/* -IN      : inode  current inode-struct of device-access                   */
/*            file   current file-struct of device-access                    */
/*            cmd    command-code for direct pc-port-accesses to la-card     */
/*            arg    RD/WR-Data to la-card (0000h .... FFFFh)                */
/* -OUT     :  -                                                             */
/* -RETURN  : error  =0 o.k.; negativ -> errno.h                             */
/*                                                                           */
/* Variables                                                                 */
/* -external:  -                                                             */
/* -global  : la_table[] vector of opened subdevice vectorfields             */
/* -local   : lax        ptr to currently used subdevice-parameterfield      */
/*            major      current major-nbr of this device -> LOGAN-Card      */
/*            minor      current minor-nbr of this subdevice -> logan,ad/da  */
/*                                                                           */
/*****************************************************************************/
static int la_ioctl(struct inode * inode, struct file * file,              
             unsigned int cmd, unsigned long arg)                   
{
	int minor, major;
	struct la_struct * lax;

    major = MAJOR(file->f_rdev);
    minor = MINOR(file->f_rdev);
    
#ifdef LA_DEBUG
    printk("la_ioctl: try to access device: Major= %d Minor= %d\n",
            major, minor);
#endif
   
	/* break if wrong major-nbr is detected */
	if (major != LA_MAJOR)
	 {	printk("la_ioctl: bad pseudo-major nr #%d\n", major);
		return -EINVAL;
	 }
	 
	lax = la_table[minor];      /* get the competent parameter-field-ptr */
	
	if (lax == NULL)     /* if subdevice not opened */
	 {	printk("la_ioctl: device: la%d not open \n",minor );
		return -ENODEV; 
	 }

	/* call if spec. ioctl-function is chained */	 
	if (lax->ioctl)              
		return(lax->ioctl)(lax, file, (unsigned int)cmd, (unsigned long)arg);

#ifdef LA_DEBUG
    printk("la_ioctl: no special ioctl-routine defined of la%d\n", minor);  
#endif

	return -EIO;               /* return: IO-Error */
}