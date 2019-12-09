/*****************************************************************************/
/* Header-Name: LA.H                                             12.07.1994  */
/*                                                                           */
/* Autor: Randolph Esser   7/94                                              */
/*                                                                           */
/*****************************************************************************/

#ifndef  LA_H
#define  LA_H

/*
 * LA.H defines some structures used by LA_HL.C und LA_LL.C 
 */

/*---------------------------------------------------------------------------*/
/* Global Defines: */

#include <linux/la_def.h>				/* defines for la-device */




/*---------------------------------------------------------------------------*/
/* Global Variables: */


struct la_struct {
	   int minor;               /* current minor-# of lax-struct */
       int mode;                /* access-mode of logan-card,init by ioctl() */
       int open_flags;          /* access-mode of logan-file, init by open() */
       int open_cnt;            /* number of open()-calls to logan-card */    
       unsigned int scan_adr;   /* current address of scanbuf-counter   */
       unsigned int usr_adr;    /* current address of scanbuf-counter   */
       unsigned char status;    /* current status of scanning-process   */
       unsigned char ctrl;      /* current state of control-port        */
       int  (*open)  (struct la_struct * , struct file * );
       void (*close) (struct la_struct * lax, struct file * filp);
       int  (*read)  (struct la_struct * lax, struct file * filp,
       				 unsigned char * buf, unsigned int count);
       int  (*write) (struct la_struct * lax, struct file * filp,
       				 unsigned char * buf, unsigned int count);
       int  (*select)(struct la_struct * lax, struct file * filp,
       				 int select_mode, select_table * wait);
       int  (*ioctl) (struct la_struct * lax, struct file * filp,
       				 unsigned int cmd, unsigned long arg);
       int  (*lseek) (struct la_struct * lax, struct file * filp,
       				 off_t offset, int origin);
       };              


extern struct la_struct *la_table[];


/*---------------------------------------------------------------------------*/
/* External Prototyping: */

#define IS_LA_BUSY(minor) (!((la_table[(minor)]->status) & STAT_COUNTEND))
#define IS_LA_TRIG(minor) ((la_table[(minor)]->status) & STAT_TRIGGER)

extern long la_init(long);                                                             

extern int  logan_lseek(struct la_struct *, struct file *, off_t, int);                
extern int  logan_read(struct la_struct *, struct file *,
					   unsigned  char *, unsigned int);                
extern int  logan_write(struct la_struct *, struct file *,
					 	unsigned char *, unsigned int);               
extern int  logan_select(struct la_struct *, struct file *,
						int, select_table *);      
extern int  logan_ioctl(struct la_struct *, struct file *,
						unsigned int, unsigned long); 
extern int  logan_open(struct la_struct *, struct file *);
extern void logan_close(struct la_struct *, struct file *);                          

extern void logan_rdstatus( int, struct la_struct * );      



/*---------------------------------------------------------------------------*/
/* End of Headerfile LA.H */

#endif
