
#include "math.h"

#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <semaphore.h>
#include <fcntl.h>
#include <malloc.h>
#include <unistd.h>

#include <sys/file.h>
#include <sys/mman.h>
#include <sched.h>


#include "image_shm.h"
#define SHAREDMEMDIR "/tmp"

#include "mil.h"
#include "ocam2_sdk.h"

#define SNAME "ocam2ksem"   // semaphore name
IMAGE *image;
sem_t *sem;

int RAWSAVEMODE = 0; // 0: save descrambled image, 1: save raw buffer

// Added: Number of Lines per Slice.
int NUM_LINE_SLICE = 4;
int sliceNB;
int NBslices;
long nbpixslice[100];


char const DESCRAMBLE_FILE[] = "ocam2_descrambling.txt"; char const MIL_DCF_BINNING[] = "ocam2_mil_binning.dcf"; #define IMAGE_WIDTH_RAW OCAM2_IMAGE_WIDTH_RAW_BINNING #define IMAGE_HEIGHT_RAW OCAM2_IMAGE_HEIGHT_RAW_BINNING

int IMAGE_WIDTH = 120;
int IMAGE_HEIGHT = 120;

/* MIL */
MIL_ID MilApplication;        /* Application identifier.  */
MIL_ID MilSystem;             /* System identifier.       */
MIL_ID MilDigitizer;          /* Digitizer identifier.    */
MIL_ID *MilGrabBufferList;    /* Image buffer identifier. */
MIL_ID MilMutex;




/* User's processing function hook data structure. */ typedef struct {
     MIL_ID MilDigitizer;
     MIL_INT ProcessedImageCount;
     MIL_INT ProcessedSliceCount;
     MIL_INT slice;
     ocam2_id id;
     unsigned short imagearray[OCAM2_PIXELS_IMAGE_BINNING];
     short *dataptr;
     int slice_firstelem[100];
     int slice_lastelem[100];
     short *buff1;

     long *imgcnt;

     long framecnt;

     unsigned short *pixr;
     unsigned short *pixi;
     unsigned short *rc;

     int hookinit;
     short *pMilGrabBuff;
} HookDataStruct;


void initMilError(char const errorMsg[]); void initMil(); void exitMil();
/* User's processing function prototype. */ MIL_INT MFTYPE ProcessingFunction(MIL_INT HookType, MIL_ID HookId, void* HookDataPtr); MIL_INT MFTYPE Grab_nth_Line(MIL_INT HookType, MIL_ID EventId, void* HookDataPtr);


/* Number of images in the buffering grab queue.
Generally, increasing this number gives a better real-time grab.
*/
#define BUFFERING_SIZE_MAX 22


ocam2_id ocamid;






//  clock_gettime(CLOCK_REALTIME, &tnow);

/* tdiff = info_time_diff(data.image[aoconfID_looptiming].md[0].wtime,
tnow);
             tdiffv = 1.0*tdiff.tv_sec + 1.0e-9*tdiff.tv_nsec;
             data.image[aoconfID_looptiming].array.F[20] = tdiffv; */



struct timespec info_time_diff(struct timespec start, struct timespec end)
{
   struct timespec temp;
   if ((end.tv_nsec-start.tv_nsec)<0) {
     temp.tv_sec = end.tv_sec-start.tv_sec-1;
     temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
   } else {
     temp.tv_sec = end.tv_sec-start.tv_sec;
     temp.tv_nsec = end.tv_nsec-start.tv_nsec;
   }
   return temp;
}




long create_image_shm(long ID, char *name, long naxis, long *size, int 
atype, int NBkw)
{
     long i,ii;
//    time_t lt;
     long nelement;
     struct timespec timenow;

     size_t sharedsize = 0; // shared memory size in bytes
     int SM_fd; // shared memory file descriptor
     char SM_fname[200];
     int result;
     IMAGE_METADATA *map;
     char *mapv; // pointed cast in bytes

     int kw;
//    char comment[80];
//    char kname[16];


     image[ID].used = 1;

     nelement = 1;
     for(i=0; i<naxis; i++)
         nelement*=size[i];

     // compute total size to be allocated

     sharedsize = sizeof(IMAGE_METADATA);

     if(atype==CHAR)
         sharedsize += nelement*sizeof(char);
     if(atype==INT)
         sharedsize += nelement*sizeof(int);
     if(atype==FLOAT)
         sharedsize += nelement*sizeof(float);
     if(atype==DOUBLE)
         sharedsize += nelement*sizeof(double);
     if(atype==COMPLEX_FLOAT)
         sharedsize += nelement*2*sizeof(float);
     if(atype==COMPLEX_DOUBLE)
         sharedsize += nelement*2*sizeof(double);
     if(atype==USHORT)
         sharedsize += nelement*sizeof(unsigned short int);
     if(atype==LONG)
         sharedsize += nelement*sizeof(long);

     sharedsize += NBkw*sizeof(IMAGE_KEYWORD);


     sprintf(SM_fname, "%s/%s.im.shm", SHAREDMEMDIR, name);
     printf("CREATING SHARED MEMORY IMAGE %s\n", SM_fname);
     SM_fd = open(SM_fname, O_RDWR | O_CREAT | O_TRUNC, (mode_t)0600);

     if (SM_fd == -1) {
         perror("Error opening file for writing");
         exit(0);
     }
     image[ID].shmfd = SM_fd;
     image[ID].memsize = sharedsize;

     result = lseek(SM_fd, sharedsize-1, SEEK_SET);
     if (result == -1) {
         close(SM_fd);
         perror("Error calling lseek() to 'stretch' the file");
         exit(0);
     }

     result = write(SM_fd, "", 1);
     if (result != 1) {
         close(SM_fd);
         perror("Error writing last byte of the file");
         exit(0);
     }

     map = (IMAGE_METADATA*) mmap(0, sharedsize, PROT_READ | PROT_WRITE, 
MAP_SHARED, SM_fd, 0);
     if (map == MAP_FAILED) {
         close(SM_fd);
         perror("Error mmapping the file");
         exit(0);
     }

     image[ID].md = (IMAGE_METADATA*) map;
     image[ID].md[0].shared = 1;




     image[ID].md[0].atype = atype;
     image[ID].md[0].naxis = naxis;
     strcpy(image[ID].md[0].name, name);
     for(i=0; i<naxis; i++)
         image[ID].md[0].size[i] = size[i];
     image[ID].md[0].NBkw = NBkw;




     if(atype==CHAR)
     {
         mapv = (char*) map;
         mapv += sizeof(IMAGE_METADATA);
         image[ID].array.C = (char*) (mapv);
         memset(image[ID].array.C, '\0', nelement*sizeof(char));
         mapv += sizeof(char)*nelement;
         image[ID].kw = (IMAGE_KEYWORD*) (mapv);


         if(image[ID].array.C == NULL)
         {
             printf("memory allocation failed");
             fprintf(stderr,"%c[%d;%dm", (char) 27, 1, 31);
             fprintf(stderr,"Image name = %s\n",name);
             fprintf(stderr,"Image size = ");
             fprintf(stderr,"%ld",size[0]);
             for(i=1; i<naxis; i++)
                 fprintf(stderr,"x%ld",size[i]);
             fprintf(stderr,"\n");
             fprintf(stderr,"Requested memory size = %ld elements = %f 
Mb\n",nelement,1.0/1024/1024*nelement*sizeof(char));
             fprintf(stderr," %c[%d;m",(char) 27, 0);
             exit(0);
         }
     }
     if(atype==INT)
     {
         mapv = (char*) map;
         mapv += sizeof(IMAGE_METADATA);
         image[ID].array.I = (int*) (mapv);
         memset(image[ID].array.I, '\0', nelement*sizeof(int));
         mapv += sizeof(int)*nelement;
         image[ID].kw = (IMAGE_KEYWORD*) (mapv);

         if(image[ID].array.I == NULL)
         {
             printf("memory allocation failed");
             fprintf(stderr,"%c[%d;%dm", (char) 27, 1, 31);
             fprintf(stderr,"Image name = %s\n",name);
             fprintf(stderr,"Image size = ");
             fprintf(stderr,"%ld",size[0]);
             for(i=1; i<naxis; i++)
                 fprintf(stderr,"x%ld",size[i]);
             fprintf(stderr,"\n");
             fprintf(stderr,"Requested memory size = %ld elements = %f 
Mb\n",nelement,1.0/1024/1024*nelement*sizeof(int));
             fprintf(stderr," %c[%d;m",(char) 27, 0);
             exit(0);
         }
     }
     if(atype==LONG)
     {
         mapv = (char*) map;
         mapv += sizeof(IMAGE_METADATA);
         image[ID].array.L = (long*) (mapv);
         memset(image[ID].array.L, '\0', nelement*sizeof(long));
         mapv += sizeof(long)*nelement;
         image[ID].kw = (IMAGE_KEYWORD*) (mapv);

         if(image[ID].array.L == NULL)
         {
             printf("memory allocation failed");
             fprintf(stderr,"%c[%d;%dm", (char) 27, 1, 31);
             fprintf(stderr,"Image name = %s\n",name);
             fprintf(stderr,"Image size = ");
             fprintf(stderr,"%ld",size[0]);
             for(i=1; i<naxis; i++)
                 fprintf(stderr,"x%ld",size[i]);
             fprintf(stderr,"\n");
             fprintf(stderr,"Requested memory size = %ld elements = %f 
Mb\n",nelement,1.0/1024/1024*nelement*sizeof(long));
             fprintf(stderr," %c[%d;m",(char) 27, 0);
             exit(0);
         }
     }
     if(atype==FLOAT)    {
         mapv = (char*) map;
         mapv += sizeof(IMAGE_METADATA);
         image[ID].array.F = (float*) (mapv);
         memset(image[ID].array.F, '\0', nelement*sizeof(float));
         mapv += sizeof(float)*nelement;
         image[ID].kw = (IMAGE_KEYWORD*) (mapv);

         if(image[ID].array.F == NULL)
         {
             printf("memory allocation failed");
             fprintf(stderr,"%c[%d;%dm", (char) 27, 1, 31);
             fprintf(stderr,"Image name = %s\n",name);
             fprintf(stderr,"Image size = ");
             fprintf(stderr,"%ld",size[0]);
             for(i=1; i<naxis; i++)
                 fprintf(stderr,"x%ld",size[i]);
             fprintf(stderr,"\n");
             fprintf(stderr,"Requested memory size = %ld elements = %f 
Mb\n",nelement,1.0/1024/1024*nelement*sizeof(float));
             fprintf(stderr," %c[%d;m",(char) 27, 0);
             exit(0);
         }
     }
     if(atype==DOUBLE)
     {
         mapv = (char*) map;
         mapv += sizeof(IMAGE_METADATA);
         image[ID].array.D = (double*) (mapv);
         memset(image[ID].array.D, '\0', nelement*sizeof(double));
         mapv += sizeof(double)*nelement;
         image[ID].kw = (IMAGE_KEYWORD*) (mapv);

         if(image[ID].array.D == NULL)
         {
             printf("memory allocation failed");
             fprintf(stderr,"%c[%d;%dm", (char) 27, 1, 31);
             fprintf(stderr,"Image name = %s\n",name);
             fprintf(stderr,"Image size = ");
             fprintf(stderr,"%ld",size[0]);
             for(i=1; i<naxis; i++)
                 fprintf(stderr,"x%ld",size[i]);
             fprintf(stderr,"\n");
             fprintf(stderr,"Requested memory size = %ld elements = %f 
Mb\n",nelement,1.0/1024/1024*nelement*sizeof(double));
             fprintf(stderr," %c[%d;m",(char) 27, 0);
             exit(0);
         }
     }
     if(atype==COMPLEX_FLOAT)
     {
         mapv = (char*) map;
         mapv += sizeof(IMAGE_METADATA);
         image[ID].array.CF = (complex_float*) (mapv);
         memset(image[ID].array.CF, '\0', nelement*sizeof(complex_float));
         mapv += sizeof(complex_float)*nelement;
         image[ID].kw = (IMAGE_KEYWORD*) (mapv);

         for(ii=0; ii<nelement; ii++)
         {
             image[ID].array.CF[ii].re = 0.0;
             image[ID].array.CF[ii].im = 0.0;
         }
         if(image[ID].array.CF == NULL)
         {
             printf("memory allocation failed");
             fprintf(stderr,"%c[%d;%dm", (char) 27, 1, 31);
             fprintf(stderr,"Image name = %s\n",name);
             fprintf(stderr,"Image size = ");
             fprintf(stderr,"%ld",size[0]);
             for(i=1; i<naxis; i++)
                 fprintf(stderr,"x%ld",size[i]);
             fprintf(stderr,"\n");
             fprintf(stderr,"Requested memory size = %ld elements = %f 
Mb\n",nelement,1.0/1024/1024*nelement*sizeof(float)*2);
             fprintf(stderr," %c[%d;m",(char) 27, 0);
             exit(0);
         }
     }
     if(atype==COMPLEX_DOUBLE)
     {
         mapv = (char*) map;
         mapv += sizeof(IMAGE_METADATA);
         image[ID].array.CD = (complex_double*) (mapv);
         memset(image[ID].array.CD, '\0', nelement*sizeof(complex_double));
         mapv += sizeof(complex_double)*nelement;
         image[ID].kw = (IMAGE_KEYWORD*) (mapv);

         if(image[ID].array.CD == NULL)
         {
             printf("memory allocation failed");
             fprintf(stderr,"%c[%d;%dm", (char) 27, 1, 31);
             fprintf(stderr,"Image name = %s\n",name);
             fprintf(stderr,"Image size = ");
             fprintf(stderr,"%ld",size[0]);
             for(i=1; i<naxis; i++)
                 fprintf(stderr,"x%ld",size[i]);
             fprintf(stderr,"\n");
             fprintf(stderr,"Requested memory size = %ld elements = %f 
Mb\n",nelement,1.0/1024/1024*nelement*sizeof(double)*2);
             fprintf(stderr," %c[%d;m",(char) 27, 0);
             exit(0);
         }
     }
     if(atype==USHORT)
     {
         mapv = (char*) map;
         mapv += sizeof(IMAGE_METADATA);
         image[ID].array.U = (unsigned short*) (mapv);
         memset(image[ID].array.U, '\0', nelement*sizeof(unsigned short));
         mapv += sizeof(unsigned short)*nelement;
         image[ID].kw = (IMAGE_KEYWORD*) (mapv);

         if(image[ID].array.U == NULL)
         {
             printf("memory allocation failed");
             fprintf(stderr,"%c[%d;%dm", (char) 27, 1, 31);
             fprintf(stderr,"Image name = %s\n",name);
             fprintf(stderr,"Image size = ");
             fprintf(stderr,"%ld",size[0]);
             for(i=1; i<naxis; i++)
                 fprintf(stderr,"x%ld",size[i]);
             fprintf(stderr,"\n");
             fprintf(stderr,"Requested memory size = %ld elements = %f 
Mb\n",nelement,1.0/1024/1024*nelement*sizeof(double)*2);
             fprintf(stderr," %c[%d;m",(char) 27, 0);
             exit(0);
         }
     }

     clock_gettime(CLOCK_REALTIME, &timenow);
     image[ID].md[0].last_access = 1.0*timenow.tv_sec + 
0.000000001*timenow.tv_nsec;
     image[ID].md[0].creation_time = image[ID].md[0].last_access;
     image[ID].md[0].write = 0;
     image[ID].md[0].cnt0 = 0;
     image[ID].md[0].cnt1 = 0;
     image[ID].md[0].nelement = nelement;



     // initialize keywords (test)
     for(kw=0; kw<image[ID].md[0].NBkw; kw++)
         image[ID].kw[kw].type = 'N';

     return ID;
}





// create semaphore(s) for image ID
long COREMOD_MEMORY_image_set_createsem(long ID, long NBsem)
{
     char sname[200];
     long s, s1;
//    int r;
  //   char command[200];
     char fname[200];
//    int semfile[100];

     if(image[ID].sem!=NBsem)
     {
         for(s=0; s<image[ID].sem; s++)
             sem_close(image[ID].semptr[s]);
         image[ID].sem = 0;

         for(s1=NBsem; s1<100; s1++)
         {
             sprintf(fname, "/dev/shm/sem.%s_sem%02ld", 
image[ID].md[0].name, s1);
             remove(fname);
         }
         free(image[ID].semptr);
         image[ID].semptr = NULL;
     }


     if(image[ID].sem == 0)
     {
         if(image[ID].semptr!=NULL)
             free(image[ID].semptr);

         image[ID].sem = NBsem;
         printf("malloc semptr %d entries\n", image[ID].sem);
         image[ID].semptr = (sem_t**) malloc(sizeof(sem_t**)*image[ID].sem);


         for(s=0; s<NBsem; s++)
         {
             sprintf(sname, "%s_sem%02ld", image[ID].md[0].name, s);
             if ((image[ID].semptr[s] = sem_open(sname, 0, 0644, 0))== 
SEM_FAILED) {
                 if ((image[ID].semptr[s] = sem_open(sname, O_CREAT, 
0644, 1)) == SEM_FAILED) {
                     perror("semaphore initilization");
                 }
                 else
                     sem_init(image[ID].semptr[s], 1, 0);
             }
         }
     }

     //printf("sem  = %d\n", data.image[ID].sem);

     return(ID);
}
























int OcamInit()
{
     ocam2_rc rc;

     rc = ocam2_init(OCAM2_BINNING, DESCRAMBLE_FILE, &ocamid);

     if (OCAM2_OK == rc)
     {
         printf("ocam2_init: success, get id:%d\n", ocamid);
         printf("Mode is: %s \n", ocam2_modeStr(ocam2_getMode(ocamid)));

     }
     else
         exit (EXIT_FAILURE);

     return(0);
}





void initMilError(char const errorMsg[])
{
     printf("MIL init ERROR: %s\n", errorMsg);
     exitMil();
     exit (EXIT_FAILURE);
}



void exitMil()
{
     long i;

     for(i=0; i<BUFFERING_SIZE_MAX; i++)
     {
         if (M_NULL!=MilGrabBufferList[i])
             MbufFree(MilGrabBufferList[i]);
     }

     free(MilGrabBufferList);

     if (M_NULL!=MilDigitizer)
         MdigFree(MilDigitizer);
     if (M_NULL!=MilMutex)
         MthrFree(MilMutex);
     if (M_NULL!=MilSystem)
         MsysFree(MilSystem);
     if (M_NULL!=MilApplication)
         MappFree(MilApplication);
}


int main(int argc, char *argv[])
{
     ocam2_rc rc;
//    MIL_INT MilGrabBufferListSize;
     MIL_INT ProcessFrameCount = 0;
     MIL_DOUBLE ProcessFrameRate = 0;
//    MIL_INT NbFrames = 0, n = 0;
     HookDataStruct UserHookData;
     MIL_INT64 BufSizeX;
     MIL_INT64 BufSizeY;
     int i;
     unsigned int number;
     int naxis = 3;
     long *size;
     long ii, jj;
     short *buffraw1;
//    short *buffraw2;
     short *buffim1;
//    short *buffim2;

     long cntpix;
     long maxcnt;

     FILE *fp;

     int RT_priority = 80; //any number from 0-99
     struct sched_param schedpar;


     if (argc != 2)
     {
         printf("%s takes 1 argument: Number of frame grabber lines per 
read\n", argv[0]);
         exit(0);
     }
     else
     {
         NUM_LINE_SLICE = atoi(argv[1]);
         printf("NUM_LINE_SLICE = %d\n", NUM_LINE_SLICE);
     }


     schedpar.sched_priority = RT_priority;
     sched_setscheduler(0, SCHED_FIFO, &schedpar); //other option is 
SCHED_RR, might be faster


     image = (IMAGE*) malloc(sizeof(IMAGE)*100); // 100 images max

     size = (long*) malloc(sizeof(long)*3);
     if(RAWSAVEMODE==1)
     {
         size[0] = OCAM2_IMAGE_WIDTH_RAW_BINNING;
         size[1] = OCAM2_IMAGE_HEIGHT_RAW_BINNING;
     }
     else
     {
         size[0] = IMAGE_WIDTH;
         size[1] = IMAGE_HEIGHT;
     }
     //  size[2] = 5;  // circular buffer depth

     create_image_shm(0, "ocam2k", 2, size, USHORT, 0);
     COREMOD_MEMORY_image_set_createsem(0, 2);

     image[0].md[0].status=1;


     if ((sem = sem_open(SNAME, O_CREAT, 0644, 1)) == SEM_FAILED) {
         perror("semaphore initilization");
         exit(1);
     }
     UserHookData.dataptr = (short*) image[0].array.U;
     UserHookData.imgcnt = &image[0].md[0].cnt0;


     printf("\nOcam2 sdk version:%s  build:%s\n",ocam2_sdkVersion(), 
ocam2_sdkBuild());
     OcamInit();
     UserHookData.id = ocamid;

     printf("OCAM2_IMAGE_WIDTH_RAW_BINNING = %d\n", 
OCAM2_IMAGE_WIDTH_RAW_BINNING);
     printf("OCAM2_IMAGE_HEIGHT_RAW_BINNING = %d\n", 
OCAM2_IMAGE_HEIGHT_RAW_BINNING);
     printf("OCAM2_PIXELS_IMAGE_BINNING = %d\n", 
OCAM2_PIXELS_IMAGE_BINNING);

     printf("OCAM2_IMAGE_NB_OFFSET = %d\n", OCAM2_IMAGE_NB_OFFSET);





     // pixel mapping

     NBslices = OCAM2_IMAGE_HEIGHT_RAW_BINNING/NUM_LINE_SLICE/2+1;
     if(NBslices>31)
         NBslices = 31;
     printf("NBslices = %d  ( %d / %d)\n", NBslices, 
OCAM2_IMAGE_HEIGHT_RAW_BINNING, NUM_LINE_SLICE);
     printf("%d raw pixel per slice\n", 
NUM_LINE_SLICE*OCAM2_IMAGE_WIDTH_RAW_BINNING);
     fflush(stdout);
     if(1)
     {
         buffraw1 = (short*) 
malloc(sizeof(short)*OCAM2_IMAGE_WIDTH_RAW_BINNING*OCAM2_IMAGE_HEIGHT_RAW_BINNING);
         buffim1 = (short*) malloc(sizeof(short)*IMAGE_WIDTH*IMAGE_HEIGHT);

         for(ii=0; 
ii<OCAM2_IMAGE_WIDTH_RAW_BINNING*OCAM2_IMAGE_HEIGHT_RAW_BINNING; ii++)
             buffraw1[ii] = ii;
         maxcnt = 10;
         ocam2_descramble(UserHookData.id, &number, buffim1, buffraw1);


         for(sliceNB=0; sliceNB<NBslices; sliceNB++)
             {
                 nbpixslice[sliceNB] = 0;
                 UserHookData.slice_firstelem[sliceNB] = 100000000;
                 UserHookData.slice_lastelem[sliceNB] = 0;
             }
         for(ii=0; ii<IMAGE_WIDTH*IMAGE_HEIGHT; ii++)
         {
             sliceNB = 
buffim1[ii]/(NUM_LINE_SLICE*OCAM2_IMAGE_WIDTH_RAW_BINNING);
             if(sliceNB<NBslices)
                 nbpixslice[sliceNB]++;
         }
         cntpix = 0;
         maxcnt = 0;
         if ((fp=fopen("pixsliceNB.txt", "w"))==NULL)
         {
             printf("ERROR: cannot create file pixsliceNB.txt\n");
             exit(0);
         }
         for(sliceNB=0; sliceNB<NBslices; sliceNB++)
         {
             if(nbpixslice[sliceNB]>maxcnt)
                 maxcnt = nbpixslice[sliceNB];
             cntpix += nbpixslice[sliceNB];
           //  printf("Slice %2d : %5ld pix    [cumul = %5ld]\n", 
sliceNB, nbpixslice[sliceNB], cntpix);
             fprintf(fp, "%d %ld %ld\n", sliceNB, nbpixslice[sliceNB], 
cntpix);
         }
         fclose(fp);

         naxis = 3;

         size[0] = (long) (sqrt(maxcnt)+1.0);
         size[1] = (long) (sqrt(maxcnt)+1.0);
         size[2] = NBslices;

         printf("FORMAT = %ld %ld %ld\n", size[0], size[1], size[2]);

         create_image_shm(1, "ocam2krc", naxis, size, USHORT, 0); // raw 
cube (data)
         COREMOD_MEMORY_image_set_createsem(1, 2);
         create_image_shm(2, "ocam2kpixr", naxis, size, USHORT, 0); // 
pixel mapping: index in raw
         COREMOD_MEMORY_image_set_createsem(2, 2);
         create_image_shm(3, "ocam2kpixi", naxis, size, USHORT, 0); // 
pixel mapping: index in image
         COREMOD_MEMORY_image_set_createsem(3, 2);

         UserHookData.rc = image[1].array.U;
         UserHookData.pixr = image[2].array.U;
         UserHookData.pixi = image[3].array.U;

         for(sliceNB=0; sliceNB<NBslices; sliceNB++)
             nbpixslice[sliceNB] = 0;
         for(ii=0; ii<IMAGE_WIDTH*IMAGE_HEIGHT; ii++)
         {
             sliceNB = 
buffim1[ii]/(NUM_LINE_SLICE*OCAM2_IMAGE_WIDTH_RAW_BINNING);
             jj = size[0]*size[1]*sliceNB+nbpixslice[sliceNB];
             image[2].array.U[jj] = buffim1[ii];
             image[3].array.U[jj] = ii;
             if(buffim1[ii]<UserHookData.slice_firstelem[sliceNB])
                 UserHookData.slice_firstelem[sliceNB] = buffim1[ii];
              if(buffim1[ii]>UserHookData.slice_lastelem[sliceNB])
                 UserHookData.slice_lastelem[sliceNB] = buffim1[ii];

             nbpixslice[sliceNB]++;
         }

         for(ii=0; 
ii<OCAM2_IMAGE_WIDTH_RAW_BINNING*OCAM2_IMAGE_HEIGHT_RAW_BINNING; ii++)
             buffraw1[ii] = 0;
         ocam2_descramble(UserHookData.id, &number, buffim1, buffraw1);


         for(sliceNB=0; sliceNB<NBslices; sliceNB++)
             printf("Slice %2d : %5ld pix    [cumul = %5ld]   raw pix   
: %6d -> %6d\n", sliceNB, nbpixslice[sliceNB], cntpix, 
UserHookData.slice_firstelem[sliceNB], 
UserHookData.slice_lastelem[sliceNB]);

         free(buffraw1);
         free(buffim1);
     }


     UserHookData.buff1 = (short*) 
malloc(sizeof(short)*(OCAM2_IMAGE_WIDTH_RAW_BINNING*OCAM2_IMAGE_HEIGHT_RAW_BINNING*NBslices+12));
     UserHookData.framecnt = 0;
     UserHookData.hookinit = 0;

     printf("Setting up frame grabber step 00\n");
     fflush(stdout);

     MilGrabBufferList = (MIL_ID*) 
malloc(sizeof(MIL_ID)*BUFFERING_SIZE_MAX);
     if(MilGrabBufferList==NULL)
         initMilError("CANNOT ALLOCATE MilGrabBufferList");

     printf("Setting up frame grabber step 01\n");
     fflush(stdout);

     /* Allocate a MIL application. */
     if (M_NULL == MappAlloc(MIL_TEXT("M_DEFAULT"), M_DEFAULT, 
&MilApplication))
         initMilError("MappAlloc failed !");

     printf("Setting up frame grabber step 02\n");
     fflush(stdout);


     /* Allocate a MIL system. */
     if (M_NULL == MsysAlloc(M_DEFAULT, MT("M_DEFAULT"), M_DEFAULT, 
M_DEFAULT, &MilSystem))
         initMilError("MsysAlloc failed !");

     /* Allow MIL to call ProcessingFunction in several thread. */
     MsysControl(MilSystem, M_MODIFIED_BUFFER_HOOK_MODE, M_MULTI_THREAD 
+ BUFFERING_SIZE_MAX);


     /* Allocate a MIL digitizer if supported and sets the target image 
size. */
     if (MsysInquire(MilSystem, M_DIGITIZER_NUM, M_NULL) > 0)
     {
         if (M_NULL == MdigAlloc(MilSystem, M_DEFAULT, 
"ocam2_mil_binning.dcf", M_DEFAULT, &MilDigitizer))
             initMilError("MdigAlloc failed !");

         MdigInquire(MilDigitizer, M_SIZE_X, &BufSizeX);
         MdigInquire(MilDigitizer, M_SIZE_Y, &BufSizeY);

         printf("Frame grabber image size : %ld %ld\n", BufSizeX, BufSizeY);
         fflush(stdout);
         if ( (IMAGE_WIDTH_RAW!=BufSizeX) || (IMAGE_HEIGHT_RAW!=BufSizeY) )
             initMilError("Dcf file informations(height or width) 
invalid !");
     }
     else
         initMilError("Can't find a grabber, exiting...");

     /* Allocate the grab buffers and clear them. */
     for(i = 0; i<BUFFERING_SIZE_MAX; i++)
     {
         MbufAlloc2d(MilSystem,
                     MdigInquire(MilDigitizer, M_SIZE_X, M_NULL),
                     MdigInquire(MilDigitizer, M_SIZE_Y, M_NULL),
                     MdigInquire(MilDigitizer, M_TYPE, M_NULL),
                     M_IMAGE+M_GRAB,
                     &MilGrabBufferList[i]);

         if (MilGrabBufferList[i])
         {
             MbufClear(MilGrabBufferList[i], 0xff);
         }
         else
             initMilError("MbufAlloc2d failed !");
     }

     /* MIL event allocation for grab end hook. */
     if (M_NULL ==  MthrAlloc(MilSystem, M_MUTEX, M_DEFAULT, M_NULL, 
M_NULL, &MilMutex))
         initMilError("MthrAlloc failed !");

     /* Print a message. */
     MosPrintf(MIL_TEXT("\nMULTIPLE BUFFERED PROCESSING.\n"));
     MosPrintf(MIL_TEXT("-----------------------------\n\n"));
     MosPrintf(MIL_TEXT("Press <Enter> to start processing.\r"));

     printf(" =========== %ld %ld\n\n\n\n", image[1].md[0].size[0], 
image[1].md[0].size[1]);
     fflush(stdout);
     /* Grab continuously on the display and wait for a key press. */
     /*  MdigGrabContinuous(MilDigitizer, MilImageDisp);
     MosGetch();
     */
     /* Halt continuous grab. */
     // MdigHalt(MilDigitizer);

     /* Initialize the user's processing function data structure. */
     UserHookData.MilDigitizer = MilDigitizer;
     UserHookData.ProcessedImageCount = 0;



     for(sliceNB=0; sliceNB<NBslices; sliceNB++)
         MdigHookFunction(MilDigitizer, 
M_GRAB_LINE_END+((NUM_LINE_SLICE)*sliceNB), Grab_nth_Line, &UserHookData);


     /* Start the processing. The processing function is called with 
every frame grabbed. */
     MdigProcess(MilDigitizer, MilGrabBufferList, BUFFERING_SIZE_MAX, 
M_START, M_DEFAULT, ProcessingFunction, &UserHookData);

     /* Print a message and wait for a key press after a minimum number 
of frames. */
     MosPrintf(MIL_TEXT("Press <Enter> to stop. \n\n"));
     MosGetch();

     /* Stop the processing. */
     MdigProcess(MilDigitizer, MilGrabBufferList, BUFFERING_SIZE_MAX, 
M_STOP, M_DEFAULT, ProcessingFunction, &UserHookData);


     MosPrintf(MIL_TEXT("Press <Enter> to restart. \n\n"));
     MosGetch();
     /* Start the processing. The processing function is called with 
every frame grabbed. */
     MdigProcess(MilDigitizer, MilGrabBufferList, BUFFERING_SIZE_MAX, 
M_START, M_DEFAULT, ProcessingFunction, &UserHookData);




  /* Print a message and wait for a key press after a minimum number of 
frames. */
     MosPrintf(MIL_TEXT("Press <Enter> to stop. \n\n"));
     MosGetch();

     /* Stop the processing. */
     MdigProcess(MilDigitizer, MilGrabBufferList, BUFFERING_SIZE_MAX, 
M_STOP, M_DEFAULT, ProcessingFunction, &UserHookData);



     /* Print statistics. */
     MdigInquire(MilDigitizer, M_PROCESS_FRAME_COUNT, &ProcessFrameCount);
     MdigInquire(MilDigitizer, M_PROCESS_FRAME_RATE, &ProcessFrameRate);
     MosPrintf(MIL_TEXT("\n\n%ld frames grabbed at %.1f frames/sec (%.1f 
ms/frame).\n"),
               ProcessFrameCount, ProcessFrameRate, 
1000.0/ProcessFrameRate);
     MosPrintf(MIL_TEXT("Press <Enter> to end.\n\n"));
     MosGetch();



     exitMil();

     rc = ocam2_exit(ocamid);
     if (OCAM2_OK == rc)
         printf("ocam2_exit: success (ocamid:%d)\n", ocamid);
     free(image);
     free(size);
     return 0;
}



/* User's processing function called every time a grab buffer is ready. */
/* -------------------------------------------------------------------- */

/* Local defines. */
#define STRING_LENGTH_MAX 20

MIL_INT MFTYPE ProcessingFunction(MIL_INT HookType, MIL_ID HookId, void* 
HookDataPtr)
{
      HookDataStruct *UserHookDataPtr = (HookDataStruct *)HookDataPtr;
     MIL_ID ModifiedBufferId;
    // MIL_TEXT_CHAR Text[STRING_LENGTH_MAX]= {MIL_TEXT('\0'),};
     short *pMilGrabBuff;
     long ii, sliceii;
     double total;
     long slice;

   //  MdigGetHookInfo(HookId, M_MODIFIED_BUFFER+M_BUFFER_ID, 
&ModifiedBufferId);
    // MbufInquire(ModifiedBufferId, M_HOST_ADDRESS, &pMilGrabBuff);

   //  UserHookDataPtr->slice = 0;

  //   UserHookDataPtr->framecnt ++;


/*
     image[0].md[0].write = 1;
     image[0].md[0].cnt0 ++;
    for(slice=0;slice<NBslices;slice++)
    {
        sliceii = slice*image[1].md[0].size[0]*image[1].md[0].size[1];
     for(ii=0;ii<nbpixslice[slice];ii++)
         image[0].array.U[ image[3].array.U[sliceii + ii] ] = 
image[1].array.U[sliceii + ii];
    }
     image[0].md[0].cnt1 = slice;
     sem_post(image[0].semptr[0]);
     image[0].md[0].write = 0;

     UserHookDataPtr->imgcnt[0]++;
     total = 0.0;
     for(ii=0; ii<IMAGE_WIDTH*IMAGE_HEIGHT; ii++)
         total += UserHookDataPtr->dataptr[ii];
     UserHookDataPtr->ProcessedImageCount++;
*/

     /* Print and draw the frame count (remove to reduce CPU usage). */
     //MosPrintf(MIL_TEXT(" ---- Processed frame #%d \n"), 
UserHookDataPtr->framecnt);
  //   MosSprintf(Text, STRING_LENGTH_MAX, MIL_TEXT("%ld"), 
UserHookDataPtr->ProcessedImageCount);

     return 0;
}





MIL_INT MFTYPE Grab_nth_Line(MIL_INT HookType, MIL_ID HookId, void* 
HookDataPtr)
{
     MIL_ID ModifiedBufferId;
     //   short *pMilGrabBuff;
     int slice;
     long ii;
     int sliceii; //, slicejj;
     HookDataStruct *UserHookDataPtr = (HookDataStruct *)HookDataPtr;
     int offset, bsize;


    // if(UserHookDataPtr->hookinit==0)
    // {
         /* Retrieve the MIL_ID of the grabbed buffer. */
         MdigGetHookInfo(HookId, M_MODIFIED_BUFFER+M_BUFFER_ID, 
&ModifiedBufferId);
         MbufInquire(ModifiedBufferId, M_HOST_ADDRESS, 
&UserHookDataPtr->pMilGrabBuff);
     //    UserHookDataPtr->hookinit = 1;
    // }

     slice = UserHookDataPtr->slice;
     offset = UserHookDataPtr->slice_firstelem[slice];
     bsize = 
UserHookDataPtr->slice_lastelem[slice]-UserHookDataPtr->slice_firstelem[slice];
     bsize += 2;

     //if(slice<NBslices)
     memcpy(UserHookDataPtr->buff1+offset, 
UserHookDataPtr->pMilGrabBuff+offset, sizeof(short)*bsize);
     //memcpy(UserHookDataPtr->buff1+offset, pMilGrabBuff+offset, 
OCAM2_IMAGE_WIDTH_RAW_BINNING*OCAM2_IMAGE_HEIGHT_RAW_BINNING);

     //UserHookData.slice_firstelem[sliceNB], 
UserHookData.slice_lastelem[sliceNB]);

     sliceii = slice*image[1].md[0].size[0]*image[1].md[0].size[1];
     //slicejj = 
slice*OCAM2_IMAGE_WIDTH_RAW_BINNING*OCAM2_IMAGE_HEIGHT_RAW_BINNING;
      //MosPrintf(MIL_TEXT("[%d  (%5ld-%5ld) ->] "), slice, sliceii, 
sliceii+nbpixslice[slice]);
    image[1].md[0].write = 1;
     image[1].md[0].cnt0 ++;
     for(ii=0; ii<nbpixslice[slice]; ii++)
         image[1].array.U[sliceii + ii] = 
UserHookDataPtr->buff1[image[2].array.U[sliceii + ii]];
     image[1].md[0].cnt1 = slice;
     sem_post(image[1].semptr[0]);
    // sem_post(image[1].semptr[1]);
     /*if(slice==NBslices)
         {
             sem_post(image[1].semptr[0]);
             image[1].md[0].cnt0 ++;
         }*/
     image[1].md[0].write = 0;
     //MosPrintf(MIL_TEXT("[->%d] "), slice);
     /* Increment the frame counter. */
     UserHookDataPtr->ProcessedImageCount++;

     if(slice==NBslices-1)
     {
         UserHookDataPtr->slice = 0;
         sem_post(image[1].semptr[1]);
         UserHookDataPtr->framecnt ++;
         MosPrintf(MIL_TEXT(" ---- Processed frame #%d   %d\r"), 
UserHookDataPtr->framecnt, M_BUFFER_ID);
     }
     else
         UserHookDataPtr->slice++;

     return(0);
}
