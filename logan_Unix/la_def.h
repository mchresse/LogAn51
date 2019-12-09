/*****************************************************************************/
/* Header-Name: LA_DEF.H                                         12.07.1994  */
/*                                                                           */
/* Autor: Randolph Esser   7/94                                              */
/*                                                                           */
/*****************************************************************************/

#ifndef  LA_DEF_H
#define  LA_DEF_H

/*
 * LA_DEF.H has some defines for la-device 
 */

#define  LA_DEBUG                   /* set debug-flag for la_hl and la_ll */


/*---------------------------------------------------------------------------*/
/* Global Constants : */

#define  IO_BASE   0x0300           /* IO-Port-Baseaddress of LOGAN16-Card   */

#define  IO_STAT   IO_BASE+0        /* IO-RD-Port of Status-Register         */
#define  IO_CTRL   IO_BASE+0        /* IO-WR-Port of Control-Register        */
#define  IO_ADRLO  IO_BASE+1        /* IO-Port of Cache-Ram-Address lower    */
#define  IO_ADRHI  IO_BASE+2        /* IO-Port of Cache-Ram-Address higher   */
#define  IO_DATLO  IO_BASE+3        /* IO-Port of Data-Register lower        */
#define  IO_DATHI  IO_BASE+4        /* IO-Port of Data-Register higher       */
#define  IO_SEL05  IO_BASE+5        /* IO-Port currently not used            */
#define  IO_SEL06  IO_BASE+6        /* IO-Port currently not used            */
#define  IO_SEL07  IO_BASE+7        /* IO-Port currently not used            */

enum {
  CTRL_CLRTRIG        = 1,      /* Bit-0: =0 reset trigger-flag              */
  CTRL_SETTRIG        = 2,      /*    -1: =0 Simulates trigger from extern   */
  CTRL_INHCLOCK       = 4,      /*    -2: =1 Stop LOGAN-Clock-Oszillator     */
  CTRL_INHWRITE       = 8,      /*    -3: =1 Stop write to ram               */
  CTRL_LOADADR        = 16,     /*    -4: =0 Load Counter-Adress from Adrport*/
  CTRL_INCADR         = 32,     /*    -5: 0>1>0 increment Counter-Adress +1  */
  CTRL_CLRADR         = 64,     /*    -6: =0 Clear Counter-Adress to '0000'  */
  CTRL_DATSEL         = 128     /*    -7: =0 Data from Ram; =1 Data from ext.*/
  } CTRL_PORT;

enum {
  STAT_NC0            = 1,      /* Bit-0: not connected                      */
  STAT_NC1            = 2,      /*    -1: not connected                      */
  STAT_NC2            = 4,      /*    -2: not connected                      */
  STAT_NC3            = 8,      /*    -3: not connected                      */
  STAT_NC4            = 16,     /*    -4: not connected                      */
  STAT_NC5            = 32,     /*    -5: not connected                      */
  STAT_COUNTEND       = 64,     /*    -6: =1 endadress of counter reached    */
  STAT_TRIGGER        = 128     /*    -7: =1 External trigger detected       */
  } STAT_PORT;

/* 
 * This buffer-size must max. be realized in user-space by user-process.
 * (pointer to this buffer over read()/write()-function-call) 
 */
#define LA_SCANBUF_MAX    0x0FFFF   /* 64k with 16bit-word scanbuffersize    */  

#define LA_USERBUF_MAX (LA_SCANBUF_MAX*2+1) /* 128 kByte user-space max.     */

#define LA_OPEN_MAX   1         /* Max. number of possible parallel open()   */


/*
 * Definition of the Subdevice-Index in la_table[]. Maximum is MAX_SUBDEV.
 */
#define LA_MINOR_LOGAN1 	1		/* Minor-Nbr of 1. LOGAN-Subdevices */ 
#define LA_MINOR_LOGAN2 	2		/* Minor-Nbr of 2. LOGAN-Subdevices */ 
#define LA_MINOR_LOGAN3 	3		/* Minor-Nbr of 3. LOGAN-Subdevices */ 
#define LA_MINOR_LOGAN4 	4		/* Minor-Nbr of 4. LOGAN-Subdevices */ 
#define LA_MINOR_ADDA1  	5		/* Minor-Nbr of 1. AD/DA-Subdevices */ 
#define LA_MINOR_ADDA2  	6		/* Minor-Nbr of 2. AD/DA-Subdevices */ 
#define LA_MINOR_ADDA3  	7		/* Minor-Nbr of 3. AD/DA-Subdevices */ 
#define LA_MINOR_ADDA4  	8		/* Minor-Nbr of 4. AD/DA-Subdevices */ 


/*
 *Definitions for ioctl()-access:  (unsigned int) cmd, (unsigned long) arg
 */
#define LA_IO_PUTCTRL	1		/* load ctrl-port of LOGAN with 'argument'   */
#define LA_IO_GETSTAT	2		/* read status-port of LOGAN (1Byte arg)     */
#define LA_IO_GETCTRL	3		/* read ctrl-port of LOGAN (1Byte arg)       */
#define LA_IO_PUTMODE	4		/* write argument to la_struct->mode         */
#define LA_IO_RDSCADR	5		/* read scanbuf-adr from la_struct->scan_adr */
#define LA_IO_RDCTRL	6		/* read ctrl-port from la_struct->ctrl to arg*/
#define LA_IO_INIT		7		/* reset LOGAN to -start clock               */
								/*                -free trigger =0           */
								/*				  -data-link set to ram-port */
								/* status: 'ready to scan'					 */
								/* argument: same as LA_IO_PUTMODE			 */
#define LA_IO_RESET		8		/* init LOGAN to  -stop clock                */
								/*                -fix  trigger =1           */
								/*				  -data-link set to ram port */
								/* status: 'ready to read data'				 */

/*
 * Definitions for scan+read-mode in la_struct->mode given by the userjob.
 * and initiated by a ioctl()-call as arguments and cmd: LA_IO_PUTMODE.
 */
								/*								Scan-   Scan- */
#define LA_SCAN_MASK    0x0100  /* 								Frequ.: time: */
#define LA_SCAN0_BASE	0x0100	/* scan at base-address 0  in:  50MHz   1.3ms */
#define LA_SCAN1_BASE	0x0101	/* (each scan-process ends       4MHz  16.3ms */
#define LA_SCAN2_BASE	0x0102	/*  at address: LA_SCANBUF_MAX)  2MHz  32.7ms */
#define LA_SCAN3_BASE	0x0103	/*                               1MHz  65.5ms */
#define LA_SCAN4_BASE	0x0104	/*                             512kHz 131.0ms */

#define LA_SCAN0_CUR	0x0110	/* scan at current address in    50MHz  1.3ms */
#define LA_SCAN1_CUR 	0x0111	/* (each scan-process ends       4MHz  16.3ms */
#define LA_SCAN2_CUR 	0x0112	/*  at address: LA_SCANBUF_MAX)  2MHz  32.7ms */
#define LA_SCAN3_CUR 	0x0113	/*                               1MHz  65.5ms */
#define LA_SCAN4_CUR 	0x0114	/*                             512kHz 131.0ms */

#define LA_RDBUF_MASK	0x0200
#define LA_RDBUF_BASE	0x0200	/* read scanbuf from 0x0000 forward to        */
                                /* 'count'/2  */
#define LA_RDBUF_END	0x0220	/* read scanbuf from 0xFFFF backward to       */
                                /* (0xFFFF-('count'/2))                       */
#define LA_RDEXT_PORT	0x0400	/* read data from external 16bit-scanport     */
#define LA_GETSTAT_PORT	0x0401	/* read data from status-port */
#define LA_GETCTRL_PORT	0x0402	/* read data from ctrl-port */

#define LA_SCAN_POLL    0x0180  /* read in polling mode-> only for read()     */


/*----------------------------------------------------------------------------*/
/* END of LA_DEF_H */

#endif