#ifndef __UTYPES_H__
#define __UTYPES_H__

#include "cc.h"

/* Object dictionary storage */

typedef struct
{
   /* Inputs (TxPDO 0x1A00 -> 0x6000) */
   struct
   {
      uint8_t LED_State;
   } Inputs;

   /* Outputs (RxPDO 0x1600 -> 0x7000) */
   struct
   {
      uint8_t LED;
   } Outputs;

} _Objects;

extern _Objects Obj;

#endif /* __UTYPES_H__ */
