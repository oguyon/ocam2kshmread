#ifndef _IMAGESHM_H
#define _IMAGESHM_H

#define CHAR 1
#define INT 2
#define FLOAT 3
#define DOUBLE 4
#define COMPLEX_FLOAT 5
#define COMPLEX_DOUBLE 6
#define USHORT 7
#define LONG 8



typedef struct
{
    float re;
    float im;
} complex_float;

typedef struct
{
    double re;
    double im;
} complex_double;


typedef struct
{
    char name[16];
    char type; // N: unused, L: long, D: double, S: 16-char string
    union {
        long numl;
        double numf;
        char valstr[16];
    } value;
    char comment[80];
} IMAGE_KEYWORD;



typedef struct
{
    char name[80];               // image name

    long naxis;                   // number of axis
    long size[3];                 // image size
    long nelement;                // number of elements in image
    int atype;                    // data type code

    double creation_time;	        // creation time (since program start)
    double last_access;           // last time the image was accessed  (since program start)
    struct timespec wtime;

    int shared; // 1 if in shared memory

    int write;                 // 1 if image is being written
    int status;
    long cnt0;                 // counter (incremented if image is updated)
    long cnt1;

    long NBkw; // number of keywords

} IMAGE_METADATA;



typedef struct      /* structure used to store data arrays */
{
    int used;
    int shmfd; // if shared memory, file descriptor
    size_t memsize; // total size in memory if shared

    IMAGE_METADATA *md;

    union
    {
        char *C;
        int *I;
        long *L;
        float *F;
        double *D;
        complex_float *CF;
        complex_double *CD;
        unsigned short int *U;
    } array;                      // pointer to data array

    IMAGE_KEYWORD *kw;

    int sem; // number of semaphores in use     
    sem_t **semptr; // semaphore array

    sem_t *semlog; // semaphore for logging

    char name[80]; // local name (can be different from name in shared memory)
   
} IMAGE;



#endif
