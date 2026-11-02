#include "ecat_app.h"
#include "ecat_slv.h"
#include "utypes.h"
#include "main.h"
#include "cmsis_os.h"

/* Number of ecat_slv() calls without SM2 (outputs) update before the stack
 * drops to SAFEOP+ERROR. With the 1 ms poll below this is roughly 150 ms.
 */
#define ECAT_WATCHDOG_CNT 150

/* Application variables mapped into the object dictionary */
_Objects Obj;

/* LED1 (PB12) is active low, anode to 3V3 through R40 */
static void led_set (uint8_t on)
{
   HAL_GPIO_WritePin (LED1_GPIO_Port, LED1_Pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void cb_get_inputs (void)
{
   Obj.Inputs.LED_State =
      (HAL_GPIO_ReadPin (LED1_GPIO_Port, LED1_Pin) == GPIO_PIN_RESET) ? 1 : 0;
}

void cb_set_outputs (void)
{
   led_set (Obj.Outputs.LED);
}

/* Called by the stack when leaving OP, put outputs in a safe state */
static void safeoutput (void)
{
   Obj.Outputs.LED = 0;
   led_set (0);
}

void ecat_app_task (void *argument)
{
   (void)argument;

   static esc_cfg_t config =
   {
      .user_arg = NULL,
      .use_interrupt = 0,
      .watchdog_cnt = ECAT_WATCHDOG_CNT,
      .set_defaults_hook = NULL,
      .pre_state_change_hook = NULL,
      .post_state_change_hook = NULL,
      .application_hook = NULL,
      .safeoutput_override = safeoutput,
      .pre_object_download_hook = NULL,
      .post_object_download_hook = NULL,
      .rxpdo_override = NULL,
      .txpdo_override = NULL,
      .esc_hw_interrupt_enable = NULL,
      .esc_hw_interrupt_disable = NULL,
      .esc_hw_eep_handler = NULL,
      .esc_check_dc_handler = NULL,
   };

   /* Blocks until the LAN9252 answers on SPI and the EEPROM is loaded */
   ecat_slv_init (&config);

   /* Free-run polling: state machine, mailbox, then outputs -> inputs */
   for (;;)
   {
      ecat_slv ();
      osDelay (1);
   }
}
