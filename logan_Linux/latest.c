/*
** Minimal funny test-program for LOGAN-HW and char-driver of device 'la1'
**
** R.Esser 08/05/1994
*/ 



#include <stdio.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/la_def.h>

#define BUFLEN 0x1000

main()
{
int fdla1, fdla2;
int i;
char UserBuf[BUFLEN];

  setbuf(stdout, 0); 

  fdla1 = open("/dev/la1", O_RDONLY, 0);
  fdla2 = open("/dev/la1", O_WRONLY, 0);
 
  printf("\n ... here is something to do with this device ...\n");
  printf("       fdla1 = %d, fdla2 = %d \n\n", fdla1, fdla2);
  fflush(stdout);

  ioctl( fdla1, LA_IO_PUTMODE, LA_SCAN0_CUR);
  lseek(fdla1, BUFLEN, 0);
    
  read( fdla1, UserBuf, BUFLEN); 
  read( fdla1, UserBuf, BUFLEN); 

  ioctl( fdla1, LA_IO_PUTMODE, LA_RDEXT_PORT);
  read( fdla1, UserBuf, 2);
  printf("USER: extern Port-data is 0x%X : 0x%X\n", UserBuf[0], UserBuf[1]); 

  
  write( fdla1, UserBuf, BUFLEN); 

  close(fdla1);  
  
} 