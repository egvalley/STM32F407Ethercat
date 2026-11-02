#ifndef __ECAT_OPTIONS_H__
#define __ECAT_OPTIONS_H__

/* SOES options for this slave. Everything not defined here falls back to the
 * defaults in Middlewares/soes/options.h.
 *
 * The SyncManager settings must match the ESI file and the SII EEPROM
 * (EtherCAT/ESI), the stack compares them with what the master writes.
 */

#define USE_FOE          0
#define USE_EOE          0

#define MBXSIZE          128
#define MBXSIZEBOOT      128
#define MBXBUFFERS       3

#define MBX0_sma         0x1000
#define MBX0_sml         MBXSIZE
#define MBX0_sme         MBX0_sma+MBX0_sml-1
#define MBX0_smc         0x26
#define MBX1_sma         MBX0_sma+MBX0_sml
#define MBX1_sml         MBXSIZE
#define MBX1_sme         MBX1_sma+MBX1_sml-1
#define MBX1_smc         0x22

#define MBX0_sma_b       0x1000
#define MBX0_sml_b       MBXSIZEBOOT
#define MBX0_sme_b       MBX0_sma_b+MBX0_sml_b-1
#define MBX0_smc_b       0x26
#define MBX1_sma_b       MBX0_sma_b+MBX0_sml_b
#define MBX1_sml_b       MBXSIZEBOOT
#define MBX1_sme_b       MBX1_sma_b+MBX1_sml_b-1
#define MBX1_smc_b       0x22

/* SM2 outputs: buffered, written by master, AL event, watchdog trigger */
#define SM2_sma          0x1100
#define SM2_smc          0x64
#define SM2_act          1
/* SM3 inputs: buffered, read by master, AL event */
#define SM3_sma          0x1180
#define SM3_smc          0x20
#define SM3_act          1

#define MAX_MAPPINGS_SM2 1
#define MAX_MAPPINGS_SM3 1

#define MAX_RXPDO_SIZE   32
#define MAX_TXPDO_SIZE   32

#endif /* __ECAT_OPTIONS_H__ */
