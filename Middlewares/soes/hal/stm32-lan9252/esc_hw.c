/*
 * Licensed under the GNU General Public License version 2 with exceptions. See
 * LICENSE file in the project root for full license information
 */

 /** \file
 * \brief
 * ESC hardware layer functions for LAN9252 on STM32 (HAL SPI).
 *
 * Ported from hal/rt-kernel-lan9252. Function to read and write commands to
 * the ESC. Used to read/write ESC registers and memory.
 */

#include "esc.h"
#include "main.h"
#include <string.h>

#ifndef BIT
#define BIT(x)                   (1UL << (x))
#endif

#define ESC_CMD_SERIAL_WRITE     0x02
#define ESC_CMD_SERIAL_READ      0x03
#define ESC_CMD_FAST_READ        0x0B
#define ESC_CMD_RESET_SQI        0xFF

#define ESC_CMD_FAST_READ_DUMMY  1
#define ESC_CMD_ADDR_INC         BIT(6)

#define ESC_PRAM_RD_FIFO_REG     0x000
#define ESC_PRAM_WR_FIFO_REG     0x020
#define ESC_PRAM_RD_ADDR_LEN_REG 0x308
#define ESC_PRAM_RD_CMD_REG      0x30C
#define ESC_PRAM_WR_ADDR_LEN_REG 0x310
#define ESC_PRAM_WR_CMD_REG      0x314

#define ESC_PRAM_CMD_BUSY        BIT(31)
#define ESC_PRAM_CMD_ABORT       BIT(30)

#define ESC_PRAM_CMD_AVAIL       BIT(0)

#define ESC_PRAM_SIZE(x)         ((x) << 16)
#define ESC_PRAM_ADDR(x)         ((x) << 0)

#define ESC_CSR_DATA_REG         0x300
#define ESC_CSR_CMD_REG          0x304

#define ESC_CSR_CMD_BUSY         BIT(31)
#define ESC_CSR_CMD_READ         (BIT(31) | BIT(30))
#define ESC_CSR_CMD_WRITE        BIT(31)
#define ESC_CSR_CMD_SIZE(x)      ((x) << 16)

#define ESC_BYTE_TEST_REG        0x064
#define ESC_BYTE_TEST_OK         0x87654321
#define ESC_HW_CFG_REG           0x074
#define ESC_HW_CFG_READY         BIT(27)

#define ESC_SPI_TIMEOUT_MS       10

extern SPI_HandleTypeDef hspi1;

static void spi_select (void)
{
   HAL_GPIO_WritePin (LAN9252_CS_GPIO_Port, LAN9252_CS_Pin, GPIO_PIN_RESET);
}

static void spi_unselect (void)
{
   HAL_GPIO_WritePin (LAN9252_CS_GPIO_Port, LAN9252_CS_Pin, GPIO_PIN_SET);
}

static void spi_write (const void *buf, uint16_t len)
{
   HAL_SPI_Transmit (&hspi1, (uint8_t *)buf, len, ESC_SPI_TIMEOUT_MS);
}

static void spi_read (void *buf, uint16_t len)
{
   HAL_SPI_Receive (&hspi1, (uint8_t *)buf, len, ESC_SPI_TIMEOUT_MS);
}

/* lan9252 single write */
static void lan9252_write_32 (uint16_t address, uint32_t val)
{
   uint8_t data[7];

   data[0] = ESC_CMD_SERIAL_WRITE;
   data[1] = ((address >> 8) & 0xFF);
   data[2] = (address & 0xFF);
   data[3] = (val & 0xFF);
   data[4] = ((val >> 8) & 0xFF);
   data[5] = ((val >> 16) & 0xFF);
   data[6] = ((val >> 24) & 0xFF);

   spi_select ();
   spi_write (data, sizeof(data));
   spi_unselect ();
}

/* lan9252 single read */
static uint32_t lan9252_read_32 (uint16_t address)
{
   uint8_t data[4];
   uint8_t result[4];

   data[0] = ESC_CMD_FAST_READ;
   data[1] = ((address >> 8) & 0xFF);
   data[2] = (address & 0xFF);
   data[3] = ESC_CMD_FAST_READ_DUMMY;

   spi_select ();
   spi_write (data, sizeof(data));
   spi_read (result, sizeof(result));
   spi_unselect ();

   return (((uint32_t)result[3] << 24) |
           ((uint32_t)result[2] << 16) |
           ((uint32_t)result[1] << 8) |
            (uint32_t)result[0]);
}

/* ESC read CSR function */
static void ESC_read_csr (uint16_t address, void *buf, uint16_t len)
{
   uint32_t value;

   value = (ESC_CSR_CMD_READ | ESC_CSR_CMD_SIZE(len) | address);
   lan9252_write_32(ESC_CSR_CMD_REG, value);

   do
   {
      value = lan9252_read_32(ESC_CSR_CMD_REG);
   } while(value & ESC_CSR_CMD_BUSY);

   value = lan9252_read_32(ESC_CSR_DATA_REG);
   memcpy(buf, (uint8_t *)&value, len);
}

/* ESC write CSR function */
static void ESC_write_csr (uint16_t address, void *buf, uint16_t len)
{
   uint32_t value = 0;

   memcpy((uint8_t*)&value, buf,len);
   lan9252_write_32(ESC_CSR_DATA_REG, value);
   value = (ESC_CSR_CMD_WRITE | ESC_CSR_CMD_SIZE(len) | address);
   lan9252_write_32(ESC_CSR_CMD_REG, value);

   do
   {
      value = lan9252_read_32(ESC_CSR_CMD_REG);
   } while(value & ESC_CSR_CMD_BUSY);
}

/* ESC read process data ram function */
static void ESC_read_pram (uint16_t address, void *buf, uint16_t len)
{
   uint32_t value;
   uint8_t * temp_buf = buf;
   uint16_t byte_offset = 0;
   uint8_t first_byte_position, temp_len, data[4];

   value = ESC_PRAM_CMD_ABORT;
   lan9252_write_32(ESC_PRAM_RD_CMD_REG, value);

   do
   {
      value = lan9252_read_32(ESC_PRAM_RD_CMD_REG);
   }while(value & ESC_PRAM_CMD_BUSY);

   value = ESC_PRAM_SIZE(len) | ESC_PRAM_ADDR(address);
   lan9252_write_32(ESC_PRAM_RD_ADDR_LEN_REG, value);

   value = ESC_PRAM_CMD_BUSY;
   lan9252_write_32(ESC_PRAM_RD_CMD_REG, value);

   do
   {
      value = lan9252_read_32(ESC_PRAM_RD_CMD_REG);
   }while((value & ESC_PRAM_CMD_AVAIL) == 0);

   /* Read first value from FIFO */
   value = lan9252_read_32(ESC_PRAM_RD_FIFO_REG);

   /* Find out first byte position and adjust the copy from that
    * according to LAN9252 datasheet and MicroChip SDK code
    */
   first_byte_position = (address & 0x03);
   temp_len = ((4 - first_byte_position) > len) ? len : (4 - first_byte_position);

   memcpy(temp_buf ,((uint8_t *)&value + first_byte_position), temp_len);
   len -= temp_len;
   byte_offset += temp_len;

   if (len == 0)
   {
      return;
   }

   spi_select ();
   /* Send command and address for fifo read */
   data[0] = ESC_CMD_FAST_READ;
   data[1] = ((ESC_PRAM_RD_FIFO_REG >> 8) & 0xFF);
   data[2] = (ESC_PRAM_RD_FIFO_REG & 0xFF);
   data[3] = ESC_CMD_FAST_READ_DUMMY;
   spi_write (data, sizeof(data));

   /* Continue reading until we have read len */
   while(len > 0)
   {
      temp_len = (len > 4) ? 4: len;
      /* Always read 4 byte, but only copy what is left of the request */
      spi_read (data, sizeof(data));
      memcpy(temp_buf + byte_offset, data, temp_len);

      len -= temp_len;
      byte_offset += temp_len;
   }
   spi_unselect ();
}

/* ESC write process data ram function */
static void ESC_write_pram (uint16_t address, void *buf, uint16_t len)
{
   uint32_t value;
   uint8_t * temp_buf = buf;
   uint16_t byte_offset = 0;
   uint8_t first_byte_position, temp_len, data[3];

   value = ESC_PRAM_CMD_ABORT;
   lan9252_write_32(ESC_PRAM_WR_CMD_REG, value);

   do
   {
      value = lan9252_read_32(ESC_PRAM_WR_CMD_REG);
   }while(value & ESC_PRAM_CMD_BUSY);

   value = ESC_PRAM_SIZE(len) | ESC_PRAM_ADDR(address);
   lan9252_write_32(ESC_PRAM_WR_ADDR_LEN_REG, value);

   value = ESC_PRAM_CMD_BUSY;
   lan9252_write_32(ESC_PRAM_WR_CMD_REG, value);

   do
   {
      value = lan9252_read_32(ESC_PRAM_WR_CMD_REG);
   }while((value & ESC_PRAM_CMD_AVAIL) == 0);

   /* Find out first byte position and adjust the copy from that
    * according to LAN9252 datasheet
    */
   first_byte_position = (address & 0x03);
   temp_len = ((4 - first_byte_position) > len) ? len : (4 - first_byte_position);

   value = 0;
   memcpy(((uint8_t *)&value + first_byte_position), temp_buf, temp_len);

   /* Write first value from FIFO */
   lan9252_write_32(ESC_PRAM_WR_FIFO_REG, value);

   len -= temp_len;
   byte_offset += temp_len;

   if (len == 0)
   {
      return;
   }

   spi_select ();
   /* Send command and address for incrementing write */
   data[0] = ESC_CMD_SERIAL_WRITE;
   data[1] = ((ESC_PRAM_WR_FIFO_REG >> 8) & 0xFF);
   data[2] = (ESC_PRAM_WR_FIFO_REG & 0xFF);
   spi_write (data, sizeof(data));

   /* Continue writing until we have written len */
   while(len > 0)
   {
      temp_len = (len > 4) ? 4 : len;
      value = 0;
      memcpy((uint8_t *)&value, (temp_buf + byte_offset), temp_len);
      /* Always write 4 byte */
      spi_write (&value, sizeof(value));

      len -= temp_len;
      byte_offset += temp_len;
   }
   spi_unselect ();
}


/** ESC read function used by the Slave stack.
 *
 * @param[in]   address     = address of ESC register to read
 * @param[out]  buf         = pointer to buffer to read in
 * @param[in]   len         = number of bytes to read
 */
void ESC_read (uint16_t address, void *buf, uint16_t len)
{
   /* Select Read function depending on address, process data ram or not */
   if (address >= 0x1000)
   {
      ESC_read_pram(address, buf, len);
   }
   else
   {
      uint16_t size;
      uint8_t *temp_buf = (uint8_t *)buf;

      while(len > 0)
      {
         /* We read maximum 4 bytes at the time */
         size = (len > 4) ? 4 : len;
         /* Make size aligned to address according to LAN9252 datasheet
          * Table 12-14 EtherCAT CSR Address VS size and MicroChip SDK code
          */
         /* If we got an odd address size is 1 , 01b 11b is captured */
         if(address & BIT(0))
         {
            size = 1;
         }
         /* If address 1xb and size != 1 and 3 , allow size 2 else size 1 */
         else if (address & BIT(1))
         {
            size = (size & BIT(0)) ? 1 : 2;
         }
         /* size 3 not valid */
         else if (size == 3)
         {
            size = 1;
         }
         /* else size is kept AS IS */
         ESC_read_csr(address, temp_buf, size);

         /* next address */
         len -= size;
         temp_buf += size;
         address += size;
      }
   }
   /* To mimic the ET1100 always providing AlEvent on every read or write */
   ESC_read_csr(ESCREG_ALEVENT,(void *)&ESCvar.ALevent,sizeof(ESCvar.ALevent));
   ESCvar.ALevent = etohs (ESCvar.ALevent);

}

/** ESC write function used by the Slave stack.
 *
 * @param[in]   address     = address of ESC register to write
 * @param[out]  buf         = pointer to buffer to write from
 * @param[in]   len         = number of bytes to write
 */
void ESC_write (uint16_t address, void *buf, uint16_t len)
{
   /* Select Write function depending on address, process data ram or not */
   if (address >= 0x1000)
   {
      ESC_write_pram(address, buf, len);
   }
   else
   {
      uint16_t size;
      uint8_t *temp_buf = (uint8_t *)buf;

      while(len > 0)
      {
         /* We write maximum 4 bytes at the time */
         size = (len > 4) ? 4 : len;
         /* Make size aligned to address according to LAN9252 datasheet
          * Table 12-14 EtherCAT CSR Address VS size  and MicroChip SDK code
          */
         /* If we got an odd address size is 1 , 01b 11b is captured */
         if(address & BIT(0))
         {
            size = 1;
         }
         /* If address 1xb and size != 1 and 3 , allow size 2 else size 1 */
         else if (address & BIT(1))
         {
            size = (size & BIT(0)) ? 1 : 2;
         }
         /* size 3 not valid */
         else if (size == 3)
         {
            size = 1;
         }
         /* else size is kept AS IS */
         ESC_write_csr(address, temp_buf, size);

         /* next address */
         len -= size;
         temp_buf += size;
         address += size;
      }
   }

   /* To mimic the ET1x00 always providing AlEvent on every read or write */
   ESC_read_csr(ESCREG_ALEVENT,(void *)&ESCvar.ALevent,sizeof(ESCvar.ALevent));
   ESCvar.ALevent = etohs (ESCvar.ALevent);
}

/* Not used by the stack, the ESC is reset through RST# in ESC_init */
void ESC_reset (void)
{

}

void ESC_init (const esc_cfg_t * config)
{
   (void)config;

   /* Hardware reset through RST#, min 200 us low according to datasheet */
   HAL_GPIO_WritePin (LAN9252_RST_GPIO_Port, LAN9252_RST_Pin, GPIO_PIN_RESET);
   HAL_Delay (2);
   HAL_GPIO_WritePin (LAN9252_RST_GPIO_Port, LAN9252_RST_Pin, GPIO_PIN_SET);

   /* The SPI interface is ready when BYTE_TEST reads the test pattern and
    * HW_CFG reports READY. The PDI itself is only operational once the EEPROM
    * (0x0140 = 0x80, SPI) has been loaded, which ecat_slv_init waits for.
    */
   while (lan9252_read_32 (ESC_BYTE_TEST_REG) != ESC_BYTE_TEST_OK)
   {
      HAL_Delay (1);
   }

   while ((lan9252_read_32 (ESC_HW_CFG_REG) & ESC_HW_CFG_READY) == 0)
   {
      HAL_Delay (1);
   }
}
