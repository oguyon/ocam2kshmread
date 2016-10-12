# ocam2kshmread

Low latency readout of Ocam2k (First Light Imaging) camera frames

Requires
 - Matrox imaging library
 - First Light Imaging SDK

## compilation

> gcc -o ocamrun -Wall -I$MILDIR/include -I$OCAM2SDKDIR/include ocam2k.c -L$MILDIR/lib -L$OCAM2SDKDIR/lib -lmil -locam2sdk -lm -lrt -lpthread
