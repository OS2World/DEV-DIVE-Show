#define INCL_32
#define INCL_DOS
#include <os2.h>
#include <pmbitmap.h>
#include <stdio.h>
#include "direct.h"



main ( int argc, char *argv[] )
   {
   HFILE hFile;
   ULONG ulNumBytes, ulFileLength;
   PBYTE pbImageBottom, pbBMP;
   ULONG rc;
   PBITMAPFILEHEADER2 pbmfHeader;

   /* Check the number of command line arguments.                            */
   if ( argc != 2 )
      {
      printf ( "usage: SHOW filein.bmp\n" );
      printf ( "   Jams ab 8-bit bitmap image directly to the upper\n" );
      printf ( "   left corner of the screen.  Clipping is not exemplified.\n" );
      printf ( "   Note this code example works only if:\n" );
      printf ( "      1) SMVDD.SYS is installed propery in four CONFIG.SYS\n" );
      printf ( "         (You can get this from the MMPM/2 install disks), and\n" );
      printf ( "      2) Your adapter has an aperture enabled, and\n" );
      printf ( "      3) Your adapter is in 8, 16, or 24 bit bit color mode, and\n" );
      printf ( "      4) Your bitmap is 8-bit only (i.e. this does no color conversion).\n" );
      return ( 0 );
      }

   /* Attempt to open up the passed filename.                                */
   if ( DosOpen ( (PSZ)argv[1], &hFile, &ulNumBytes, 0L, FILE_NORMAL,
               OPEN_ACTION_OPEN_IF_EXISTS | OPEN_ACTION_FAIL_IF_NEW,
               OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE |
               OPEN_FLAGS_SEQUENTIAL | OPEN_FLAGS_NO_CACHE, 0L ) )
      {
      printf ( "File \"%s\" not found.\n", argv[1] );
      return ( 1 );
      }

   /* Read in the entire bitmap into memory.                                 */
   DosSetFilePtr ( hFile, 0L, FILE_END, &ulFileLength );
   DosSetFilePtr ( hFile, 0L, FILE_BEGIN, &ulNumBytes );
   if ( DosAllocMem ( (PPVOID) &pbBMP, ulFileLength,
                   (ULONG) PAG_COMMIT | PAG_READ | PAG_WRITE) )
      {
      printf ( "Error while opening up some system RAM.\n" );
      DosClose ( hFile );
      return ( 1 );
      }
   DosRead ( hFile, pbBMP, ulFileLength, &ulNumBytes );
   DosClose ( hFile );
   pbmfHeader = (PBITMAPFILEHEADER2) pbBMP;
   if ( ulNumBytes!=ulFileLength || pbmfHeader->usType!=BFT_BMAP )
      {
      printf ( "\"%s\" is not a type \"BM\" bitmap, it's \"%c%c\".\n",
                     argv[1], pbmfHeader->usType, pbmfHeader->usType>>8 );
      DosFreeMem ( pbBMP );
      return ( 1 );
      }

   /* Check a few things to see if we can proceed.                           */
   if ( pbmfHeader->bmp2.cPlanes!=1 )
      {
      printf ( "Only single plane bitmaps are supported\n" );
      DosFreeMem ( pbBMP );
      return ( 1 );
      }
   if ( pbmfHeader->bmp2.cBitCount!=8 )
      {
      printf ( "Only 8 bit bitmaps are supported\n" );
      DosFreeMem ( pbBMP );
      return ( 1 );
      }
   if ( pbmfHeader->bmp2.ulCompression )
      {
      printf ( "Only uncompressed bitmaps are supported\n" );
      DosFreeMem ( pbBMP );
      return ( 1 );
      }
   if ( pbmfHeader->bmp2.cy>480 || pbmfHeader->bmp2.cx>640 )
      {
      printf ( "Bitmaps with dimensions larger than 640x480 are not supported\n" );
      DosFreeMem ( pbBMP );
      return ( 1 );
      }

   /* Remember, images are stored up-side-down in bitmap form, but the       */
   /* frame buffer needs it right-size-up -- we must invert on output!!!     */
   pbImageBottom= pbBMP + pbmfHeader->offBits + ( ( pbmfHeader->bmp2.cx *
        (pbmfHeader->bmp2.cy-1) * pbmfHeader->bmp2.cBitCount + 7 ) >> 3 );

   /* Try to get direct screen access.                                       */
   if ( DirectScreenInit () )
      {
      printf ( "The display driver doesn't suport direct screen access.\n" );
      DosFreeMem ( pbBMP );
      return ( 1 );
      }

   /* Print out information specific to your adapter.                        */
   DirectPrintInfo();

   /* Display the bitmap.                                                    */
   if ( rc = DirectScreenDisplay ( pbImageBottom, pbBMP + 14 + pbmfHeader->bmp2.cbFix,
                     pbmfHeader->bmp2.cx, pbmfHeader->bmp2.cy ) )
      printf ( "%2.2d: Aperture found okay, but unable to acquire frame buffer or blit.\n", rc );
   else
      printf ( "You should now see the bitmap on the screen.\n" );

   /* Close the video device driver.                                         */
   DirectScreenTerm();

   /* Free up the image RAM.                                                 */
   DosFreeMem ( pbBMP );
   return ( 0 );
   }
