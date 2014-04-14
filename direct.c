#define INCL_DEV
#define INCL_DOS
#define INCL_WIN
#define INCL_32
#include <os2.h>
#include <memory.h>
#include <stdio.h>               /* Used for printf only.                    */
#include "direct.h"


/* Global information about the current video device driver.                 */
struct {
       ULONG ulPhysicalAddress;  /* Physical address                         */
       ULONG ulApertureSize;     /* 1 Meg, 4 Meg or 64k                      */
       ULONG ulScanLineSize;     /* This is >= the screen width in bytes.    */
       RECTL rctlScreen;         /* Device independant co-ordinates          */
       } ApertureInfo;
HAB   hab;                     /* Need a handle to an allocation block.      */
HDC   hdc;                     /* Need a device context for the DecEsc's     */
PBYTE pbLinearAddress;         /* Holds the linear address to the video card.*/
ULONG ulTotalScreenColors= 0L; /* Holds the total number of colors.          */
HFILE hDeviceDriver;           /* Handle to the device driver to do mapping. */




/* This is the code to move data in memory directly to the video card.       */
/* Note that pbSrc initially points to the bottom of the image, so as we     */
/* work down the screen scan lines, we will work up the image.               */
/* At this point, it's as simple as writing to any other memory location,    */
/* excepting that there must be extra logic to check for bank switches.      */
/* In your code, you may want to have two routines: 1 for machines that will */
/* have full addressability to the video card (this is becomming more common */
/* since VESA and PCI give 32-bit addresses now), and 1 for machines that    */
/* have only 64KB apertures (for machines with video cards in ISA slots      */
/* -- only 24 address lines-- and 16MB+ RAM, or cards that don't support     */
/* apertures like 8514 or cards emulating 8514).                             */
/* Note that the below routines are not optimized.  Clearly, if you take     */
/* out color conversion or put the display logic right in your image         */
/* generation routine it will save a bunch of time.                          */

ULONG DirectScreenDisplay ( PBYTE pbSrc, PBYTE pbPal, ULONG ulWidth, ULONG ulHeight )
   {
   #define DEVESC_ACQUIREFB   33010L
   #define DEVESC_DEACQUIREFB 33020L
   #define DEVESC_SWITCHBANK  33030L
   ULONG ulNumBytes;
   ULONG rc = 0L;
   struct {
          ULONG  fAFBFlags;
          ULONG  ulBankNumber;
          RECTL  rctlXRegion;
          } acquireFb;

   /* Acquire the frame buffer, and switch the bank.                         */
   /* If you do not acquire the frame buffer, some other thread might snatch */
   /* the aperture away from you.  When you regain control, there's no       */
   /* telling where you'll continue blitting.                                */
   /* WARNING!  Make extra sure that you release the frame buffer!!!         */
   /* If you do not, no application will be able to update the screen again! */
   /* Note that you can not debug inside this devescape call (because you    */
   /* have the screen locked).  However, you may find it usefull to comment  */
   /* out the acquire/release calls for debugging purposes only.             */
   acquireFb.rctlXRegion.xLeft   = 0L;
   acquireFb.rctlXRegion.xRight  = 0L;
   acquireFb.rctlXRegion.yTop    = 0L;
   acquireFb.rctlXRegion.yBottom = 0L;
   acquireFb.ulBankNumber        = 0L;
   acquireFb.fAFBFlags           = 1L;  /* Acquire and switch simultaneously.*/
   if ( DevEscape ( hdc, DEVESC_ACQUIREFB, sizeof (acquireFb),
            (PBYTE) &acquireFb, (PLONG) &ulNumBytes, (PBYTE) NULL ) !=DEV_OK )
      return ( 1L );

   /* Check what the output screen color space is for the blit.              */
   switch ( ulTotalScreenColors )
      {

      /* 256 color mode routine.                                             */
      /* Note that this routine assumes the OS/2 2.x palette.  If some other */
      /* application has changed the palette, the colors will be incorrect.  */
      /* Your PM application will have to process the WM_REALIZE_PALETTE     */
      /* message to generate some sort of conversion table.  I don't do this.*/
      case 256L:
         {
         ULONG X, Y, ulAperture = ApertureInfo.ulApertureSize;
         PBYTE pbDst = pbLinearAddress;

         Y = ulHeight;
         while ( Y-- )
            {
            /* Check if this line will pass through an aperture switch.      */
            if ( ulAperture < ulWidth )
               {
               /* Move the rest of bytes for this aperture to the screen.    */
               memcpy ( pbDst, pbSrc, ulAperture );
               pbDst += ulWidth;
               pbSrc += ulWidth;

               /* Now I need to do a bank switch.                            */
               acquireFb.ulBankNumber++;
               if ( DevEscape ( hdc, DEVESC_SWITCHBANK, 4L,
                       (PBYTE) &acquireFb.ulBankNumber,
                       (PLONG) &ulNumBytes, (PBYTE) NULL ) !=DEV_OK )
                  {
                  rc = 1L;
                  break;
                  }

               /* Set up the rest of the line to move to the screen.         */
               X = ulWidth - ulAperture;

               /* Reset the linear address to the begining of the bank.      */
               pbDst = pbLinearAddress;
               ulAperture = ApertureInfo.ulApertureSize + ulAperture;
               }
            else
               /* There's room on this aperture for the whole line, so do it.*/
               X = ulWidth;

            /* Move the pixels to the screen.                                */
            memcpy ( pbDst, pbSrc, X );
            pbSrc += X;
            pbDst += X;

            /* Adjust the output line destination.                           */
            pbDst += ApertureInfo.ulScanLineSize - ulWidth;
            ulAperture -= ApertureInfo.ulScanLineSize;

            /* Adjust the input pointer to the previous line.                */
            pbSrc -= ulWidth * 2;

            /* If aperture size is non-positive, then we do a switch banks.  */
            if ( (LONG)ulAperture <= 0 )
               {
               ulAperture += ApertureInfo.ulApertureSize;
               pbDst -= ApertureInfo.ulApertureSize;
               acquireFb.ulBankNumber++;
               if ( DevEscape ( hdc, DEVESC_SWITCHBANK, 4L,
                       (PBYTE) &acquireFb.ulBankNumber,
                       (PLONG) &ulNumBytes, (PBYTE) NULL ) !=DEV_OK )
                  {
                  rc = 1L;
                  break;
                  }
               }
            }
         }
         break;


      /* 64K color mode routine.                                             */
      case 65536L:
         {
         ULONG X, Y, ulAperture = ApertureInfo.ulApertureSize;
         PUSHORT pusDst = (PUSHORT) pbLinearAddress;
         USHORT usColor;

         Y = ulHeight;
         while ( Y-- )
            {
            /* Check if this line will pass through an aperture switch.      */
            if ( ulAperture < ulWidth * 2 )
               {
               /* Move the rest of bytes for this aperture to the screen.    */
               X = ulAperture >> 1;    /* Remember, ulAperture is in bytes.  */
               while ( X-- )
                  {
                  /* First move blue into position 0000 0000 000b bbbb.      */
                  usColor = * ( pbPal + *pbSrc * 4 ) >> 3;
                  /* Now or in green into position 0000 0ggg gggb bbbb.      */
                  usColor += ( * ( pbPal + *pbSrc * 4 + 1 ) >> 2 ) << 5;
                  /* Finally or red into position rrrr rggg gggb bbbb.       */
                  usColor += ( * ( pbPal + *pbSrc * 4 + 2 ) >> 3 ) << 11;
                  /* Output the color and update the pointers.               */
                  *pusDst ++= usColor;
                  pbSrc++;
                  }

               /* Now I need to do a bank switch.                            */
               acquireFb.ulBankNumber++;
               if ( DevEscape ( hdc, DEVESC_SWITCHBANK, 4L,
                       (PBYTE) &acquireFb.ulBankNumber,
                       (PLONG) &ulNumBytes, (PBYTE) NULL ) !=DEV_OK )
                  {
                  rc = 1L;
                  break;
                  }

               /* Set up the rest of the line to move to the screen.         */
               X = ulWidth - ( ulAperture >> 1 );

               /* Reset the linear address to the begining of the bank.      */
               pusDst = (PUSHORT) pbLinearAddress;
               ulAperture = ApertureInfo.ulApertureSize + ulAperture;
               }
            else
               /* There's room on this aperture for the whole line, so do it.*/
               X = ulWidth;

            /* Move the pixels to the screen.                                */
            while ( X-- )
               {
               usColor = * ( pbPal + *pbSrc * 4 ) >> 3;
               usColor += ( * ( pbPal + *pbSrc * 4 + 1 ) >> 2 ) << 5;
               usColor += ( * ( pbPal + *pbSrc * 4 + 2 ) >> 3 ) << 11;
               *pusDst ++= usColor;
               pbSrc++;
               }

            /* Adjust the output line destination.                           */
            pusDst += ( ApertureInfo.ulScanLineSize >> 1 ) - ulWidth;
            ulAperture -= ApertureInfo.ulScanLineSize;

            /* Adjust the input pointer to the previous line.                */
            pbSrc -= ulWidth * 2;

            /* If aperture size is non-positive, then we do a switch banks.  */
            if ( (LONG)ulAperture <= 0 )
               {
               ulAperture += ApertureInfo.ulApertureSize;
               pusDst -= ( ApertureInfo.ulApertureSize ) >> 1;
               acquireFb.ulBankNumber++;
               if ( DevEscape ( hdc, DEVESC_SWITCHBANK, 4L,
                       (PBYTE) &acquireFb.ulBankNumber,
                       (PLONG) &ulNumBytes, (PBYTE) NULL ) !=DEV_OK )
                  {
                  rc = 1L;
                  break;
                  }
               }
            }
         }
         break;


      /* 16M color routine.                                                  */
      case 16777216L:
         {
         ULONG X, Y, ulAperture = ApertureInfo.ulApertureSize;
         PBYTE pbDst = pbLinearAddress;

         Y = ulHeight;
         while ( Y-- )
            {
            /* Check if this line will pass through an aperture switch.      */
            if ( ulAperture / 3 < ulWidth )
               {
               /* Move the pixels for the rest of the aperture to the screen.*/
               X= ulAperture / 3;
               while ( X-- )
                  {
                  /* Write out the blue byte.                                */
                  *pbDst ++= * ( pbPal + *pbSrc * 4 );
                  /* Write out the green byte.                               */
                  *pbDst ++= * ( pbPal + *pbSrc * 4 + 1 );
                  /* Write out the red byte.                                 */
                  *pbDst ++= * ( pbPal + *pbSrc * 4 + 2 );
                  /* Update the source pointer.                              */
                  pbSrc++;
                  }

               /* Now I need to do a bank switch.                            */
               acquireFb.ulBankNumber++;
               if ( DevEscape ( hdc, DEVESC_SWITCHBANK, 4L,
                       (PBYTE) &acquireFb.ulBankNumber,
                       (PLONG) &ulNumBytes, (PBYTE) NULL ) !=DEV_OK )
                  {
                  rc= 1L;
                  break;
                  }

               /* Set up the rest of the line to move to the screen.         */
               X= ulWidth - ( ulAperture / 3 );

               /* Reset the linear address to the begining of the bank.      */
               pbDst= pbLinearAddress;
               ulAperture= ApertureInfo.ulApertureSize + ulAperture;
               }
            else
               /* There's room on this aperture for the whole line, so do it.*/
               X= ulWidth;

            /* Move the pixels to the screen.                                */
            while ( X-- )
               {
               *pbDst ++= * ( pbPal + *pbSrc * 4 );
               *pbDst ++= * ( pbPal + *pbSrc * 4 + 1 );
               *pbDst ++= * ( pbPal + *pbSrc * 4 + 2 );
               pbSrc++;
               }

            /* Adjust the output line destination.                           */
            pbDst+= ApertureInfo.ulScanLineSize - ulWidth - ulWidth - ulWidth;
            ulAperture-= ApertureInfo.ulScanLineSize;

            /* Adjust the input pointer to the previous line.                */
            pbSrc -= ulWidth * 2;

            /* If aperture size is non-positive, then we do a switch banks.  */
            if ( (LONG)ulAperture <= 0 )
               {
               ulAperture+= ApertureInfo.ulApertureSize;
               pbDst-= ApertureInfo.ulApertureSize;
               acquireFb.ulBankNumber++;
               if ( DevEscape ( hdc, DEVESC_SWITCHBANK, 4L,
                       (PBYTE) &acquireFb.ulBankNumber,
                       (PLONG) &ulNumBytes, (PBYTE) NULL ) !=DEV_OK )
                  {
                  rc= 1L;
                  break;
                  }
               }
            }
         }
         break;


      default:
         rc= 2L;        /* Bad mode or incorrect initialization.             */
      }

   /* Release the frame buffer... this is important.                         */
   if ( DevEscape ( hdc, DEVESC_DEACQUIREFB, (ULONG)0L, (PBYTE) NULL,
                    (PLONG)0L, (PBYTE) NULL ) !=DEV_OK )
      return ( 3L );

   return ( rc );
   }



/* This routine will open up a handle to a device driver with the service    */
/* to map physical ram to a linear address (specifically SMVDD.SYS).         */
/* You can find SMVDD.SYS on the MMPM/2 diskettes.                           */

ULONG MapPhysicalToLinear ( ULONG ulPhysicalAddress )
   {
   ULONG  ulActionTaken;
   ULONG  ulDLength;
   ULONG  ulPLength;
   struct {
          ULONG   hstream;
          ULONG   hid;
          ULONG   ulFlag;
          ULONG   ulPhysAddr;
          ULONG   ulVram_length;
          } parameter;
   #pragma pack (1)
   struct {
          USHORT usXga_rng3_selector;
          ULONG  ulLinear_address;
          } ddstruct;
   #pragma pack ()

   /* Attempt to open up the device driver.                                  */
   if ( DosOpen( (PSZ)"\\DEV\\SMVDD01$", (PHFILE) &hDeviceDriver,
            (PULONG) &ulActionTaken, (ULONG)  0L, (ULONG) FILE_SYSTEM,
            OPEN_ACTION_OPEN_IF_EXISTS, OPEN_SHARE_DENYNONE  |
            OPEN_FLAGS_NOINHERIT | OPEN_ACCESS_READONLY, (ULONG) 0L)   )
      return ( 3L );

   /* Set up the parameters for the IOCtl to map the vram to linear addr.    */
   parameter.hstream       = 0L;
   parameter.hid           = 0L;
   parameter.ulFlag        = 1L;     /* Meaning MapRam. */
   parameter.ulPhysAddr    = ulPhysicalAddress;
   parameter.ulVram_length = ApertureInfo.ulApertureSize;
   ulPLength               = sizeof (parameter);
   ulDLength               = 0L;

   /* Call the IOCtl to do the map.                                          */
   if ( DosDevIOCtl ( hDeviceDriver, (ULONG)0x81,
                      (ULONG)0x42L, (PVOID)&parameter,
                      (ULONG)ulPLength, (PULONG)&ulPLength,
                      (PVOID)&ddstruct, (ULONG)6, (PULONG)&ulDLength ) )
      return ( 4L );

   /* Set the variable to the linear address, and return.                    */
   pbLinearAddress= (PBYTE) ddstruct.ulLinear_address;

   return ( 0L );
   }



/* This routine will make a call to the video device driver to see if it     */
/* supports direct video, and if it does, gets information about the card    */
/* and maps the physical address to a linear address (addressabel by ring 3).*/

ULONG DirectScreenInit ( VOID )
   {
   #define DEVESC_GETAPERTURE 33000L
   ULONG          rc;
   ULONG          ulFunction;
   LONG           lOutCount;
   HPS            hps;
   DEVOPENSTRUC   dop= {0L,(PSZ)"DISPLAY",NULL,0L,0L,0L,0L,0L,0L};

   /* Check to see that we are not already initialized.                      */
   if ( ulTotalScreenColors )
      return ( 0xffffffff );

   /* Open a presentation space so we can query the number of screen colors. */
   hab= WinInitialize ( 0L );
   hps= WinGetPS ( HWND_DESKTOP );
   hdc= DevOpenDC ( hab, OD_MEMORY, (PSZ)"*", 5L,
                                       (PDEVOPENDATA)&dop, (HDC)NULL);
   DevQueryCaps ( hdc, CAPS_COLORS, 1L, (PLONG)&ulTotalScreenColors);

   /* Determine if the devescape calls are supported DCR96 implemented.      */
   ulFunction = DEVESC_GETAPERTURE;
   if ( DevEscape( hdc, DEVESC_QUERYESCSUPPORT, 4L,
                  (PBYTE)&ulFunction, NULL, (PBYTE)NULL ) == DEV_OK )
      {
      /* The devescape calls are okay, so lets find out the aperture info.   */
      lOutCount= sizeof (ApertureInfo);
      if ( DevEscape ( hdc, DEVESC_GETAPERTURE, 0L, (PBYTE) NULL,
            &lOutCount, (PBYTE)&ApertureInfo) != DEV_OK )
         {
         ulTotalScreenColors= 0L;
         rc= 2L;
         }
      else
         {
         /* Let's take the ulScanLineSize value and change it from pixels    */
         /* to number of bytes per scan line.                                */
         if ( ulTotalScreenColors==16L )
            ApertureInfo.ulScanLineSize= ApertureInfo.ulScanLineSize >> 1;
         else if ( ulTotalScreenColors==65536L )
            ApertureInfo.ulScanLineSize= ApertureInfo.ulScanLineSize << 1;
         else if ( ulTotalScreenColors==16777216L )
            ApertureInfo.ulScanLineSize= ApertureInfo.ulScanLineSize +
            ( ApertureInfo.ulScanLineSize << 1 );
         /* else it's 256, and already the correct size.                     */
         rc= 0L;
         }
      }

   else /* DCR96 aperture calls not suppoted.                                */
      {
      ulTotalScreenColors= 0L;
      rc= 1L;
      }

   /* Release the presentation space, but keep the device contect and hab.   */
   WinReleasePS ( hps );

   /* If no error, then map the address to a ring 3 addressable one.         */
   if ( !rc )
      rc= MapPhysicalToLinear ( ApertureInfo.ulPhysicalAddress );

   return ( rc );
   }




/* This routine will terminate the instance of the device driver.            */

ULONG DirectScreenTerm ( VOID )
   {
   /* Check to see if we are actually initialized.                           */
   if ( !ulTotalScreenColors )
      return ( 1L );

   /* Reset our number of screen colors to show deinitialization.            */
   ulTotalScreenColors= 0L;

   /* Close up our device context and allocation block.                      */
   DevCloseDC ( hdc );
   WinTerminate ( hab );

   /* Close up the device driver that did the mapping to linear address.     */
   return ( DosClose ( hDeviceDriver ) );
   }



/* You can nail this routine, it's for printing aperture information only.   */

ULONG DirectPrintInfo ( VOID )
   {
   printf ( "Aperture information specific to your hardware:\n" );
   printf ( "      ulScanLineSize= %u bytes\n", ApertureInfo.ulScanLineSize );
   printf ( "   rctlScreen.xRight= %u pels\n", ApertureInfo.rctlScreen.xRight );
   printf ( "  rctlScreen.yBottom= %u pels\n", ApertureInfo.rctlScreen.yBottom );
   printf ( "    rctlScreen.xLeft= %u pels\n", ApertureInfo.rctlScreen.xLeft );
   printf ( "     rctlScreen.yTop= %u pels\n", ApertureInfo.rctlScreen.yTop );
   printf ( "          ulPhysAddr= 0x%8.8x\n", ApertureInfo.ulPhysicalAddress );
   printf ( "      ulApertureSize= %u bytes, ", ApertureInfo.ulApertureSize );
   if ( ApertureInfo.ulApertureSize >= 1048576L )
      printf ( "%u MB\n", ApertureInfo.ulApertureSize >> 20 );
   else
      printf ( "%u KB\n", ApertureInfo.ulApertureSize >> 10 );
   printf ( "         ulNumColors= %u", ulTotalScreenColors );
   if ( ulTotalScreenColors >= 1048576L )
      printf ( ", %u M\n", ulTotalScreenColors >> 20 );
   else if ( ulTotalScreenColors >= 1024L )
      printf ( ", %u K\n", ulTotalScreenColors >> 10 );
   else
      printf ( "\n" );
   return ( 0 );
   }
