#include "acquisition.h"

#include "main.h"
#include "ad7124.h"
#include "ad7124_config.h"

extern AD7124_ConfigTypeDef AD7124_Handler;
extern uint32_t AD7124_ChannelSamples[AD7124_ENABLED_CHANNELS];
extern AD7124_RegisterTypeDef configA;
extern volatile uint8_t AD7124_LastActiveChannel;

/* ADC initialization status: AD7124_OK should be 0. */
volatile int32_t adc_reset_status  = -99;
volatile int32_t adc_config_status = -99;

/* Fixed-order PCB register readback.
 * rb_seq[0] = ID
 * rb_seq[1] = CH3_MAP
 * rb_seq[2] = CFG1
 * rb_seq[3] = FILT1
 * rb_seq[4] = ADC_CONTROL
 * rb_seq[5] = ERROR_EN
 */
volatile uint32_t rb_magic = 0x12345678U;
volatile uint32_t rb_seq[6] = {0U};
volatile int32_t  rb_st_seq[6] = {-99, -99, -99, -99, -99, -99};
volatile uint8_t  rb_all_reads_ok = 0U;

/* Live conversion diagnostics. */
volatile uint32_t adc_read_errors   = 0U;
volatile uint32_t adc_samples_total = 0U;
volatile uint32_t adc_last_raw      = 0U;
volatile uint32_t adc_last_status   = 0U;
volatile uint8_t  adc_last_channel  = 0xFFU;

/* One-shot strain-input noise measurement. */
volatile uint8_t  noise_done = 0U;
volatile uint32_t noise_window_samples = 0U;
volatile uint32_t noise_raw_min = 0U;
volatile uint32_t noise_raw_max = 0U;
volatile double noise_mean_code = 0.0;
volatile double noise_std_code  = 0.0;
volatile double noise_pp_code   = 0.0;

/* Input-referred estimates use nominal AVDD = 3.3 V and PGA = 64. */
volatile double noise_mean_uV = 0.0;
volatile double noise_std_uV  = 0.0;
volatile double noise_pp_uV   = 0.0;

/* Internal Welford accumulators. */
static uint32_t noise_skip = 256U;
static uint32_t noise_n = 0U;
static uint32_t noise_min_acc = 0xFFFFFFU;
static uint32_t noise_max_acc = 0U;
static double noise_mean_acc = 0.0;
static double noise_m2_acc = 0.0;

static double NoiseSqrt(double x)
{
    if (x <= 0.0)
    {
        return 0.0;
    }
    double g = (x > 1.0) ? x : 1.0;
    for (uint8_t i = 0U; i < 16U; i++)
    {
        g = 0.5 * (g + x / g);
    }
    return g;
}

void Acquisition_Init(SPI_HandleTypeDef *spi)
{
    /*
     * PCB AD7124 bring-up test.
     *
     * Hardware under test:
     *   SPI1
     *   CS = AD7124_CS_Pin
     *   strain = CH3, AIN4(+) versus AIN5(-), Setup1
     *
     * SPI1 should be Mode 3 and /64 in the IOC for this reproduction test.
     */
    HAL_GPIO_WritePin(AD7124_CS_GPIO_Port, AD7124_CS_Pin, GPIO_PIN_SET);
    AD7124_Handler.SPIx   = spi;
    AD7124_Handler.csPort = AD7124_CS_GPIO_Port;
    AD7124_Handler.csPin  = AD7124_CS_Pin;

    /* Reproduce the known previous-PCB driver sequence. */
    adc_reset_status =
        (int32_t)AD7124_Reset(&AD7124_Handler, 100U);
    adc_config_status =
        (int32_t)AD7124_Config(&AD7124_Handler, &configA);

    /*
     * Fixed-order readback. These are the PCB-specific values we care about:
     *
     *   ID          approximately 0x06
     *   CH3_MAP     0x9085  = Setup1, AIN4 vs AIN5
     *   CFG1        0x087E  = bipolar, buffered, AVDD ref, gain 64
     *   FILT1       0x460010
     *   ADC_CONTROL 0x0080
     *   ERROR_EN    0x000004
     *
     * Every read uses the recovered driver, including its CRC checking.
     */
    uint32_t rb_tmp = 0U;
    rb_magic = 0xA5A5A5A5U;
    rb_tmp = 0U;
    rb_st_seq[0] = (int32_t)AD7124_ReadRegister(
        &AD7124_Handler, 0x05U, 1U, &rb_tmp);
    rb_seq[0] = rb_tmp;
    rb_tmp = 0U;
    rb_st_seq[1] = (int32_t)AD7124_ReadRegister(
        &AD7124_Handler, AD7124_CH3_MAP_REG, 2U, &rb_tmp);
    rb_seq[1] = rb_tmp;
    rb_tmp = 0U;
    rb_st_seq[2] = (int32_t)AD7124_ReadRegister(
        &AD7124_Handler, AD7124_CFG1_REG, 2U, &rb_tmp);
    rb_seq[2] = rb_tmp;
    rb_tmp = 0U;
    rb_st_seq[3] = (int32_t)AD7124_ReadRegister(
        &AD7124_Handler, AD7124_FILT1_REG, 3U, &rb_tmp);
    rb_seq[3] = rb_tmp;
    rb_tmp = 0U;
    rb_st_seq[4] = (int32_t)AD7124_ReadRegister(
        &AD7124_Handler, AD7124_CONTROL_REG, 2U, &rb_tmp);
    rb_seq[4] = rb_tmp;
    rb_tmp = 0U;
    rb_st_seq[5] = (int32_t)AD7124_ReadRegister(
        &AD7124_Handler, AD7124_ERREN_REG, 3U, &rb_tmp);
    rb_seq[5] = rb_tmp;
    rb_all_reads_ok = 1U;
    for (uint8_t i = 0U; i < 6U; i++)
    {
        if (rb_st_seq[i] != (int32_t)AD7124_OK)
        {
            rb_all_reads_ok = 0U;
        }
    }

    /*
     * FIRST STOP:
     * Inspect adc_reset_status, adc_config_status, rb_seq[], rb_st_seq[],
     * rb_all_reads_ok, and the AD7124_DebugLastRead* globals.
     *
     * Press Resume to continue into the strain-only noise test.
     */
}

void Acquisition_Process(void)
{
    uint32_t status_reg = 0U;

    /*
     * Read STATUS once. With the strain-only ADC config, CH3 is the only
     * enabled conversion channel.
     */
    if (AD7124_ReadRegister(
            &AD7124_Handler,
            AD7124_STATUS_REG,
            1U,
            &status_reg) != AD7124_OK)
    {
        adc_read_errors++;
        return;
    }
    adc_last_status = status_reg;

    /* STATUS.RDY is active low. */
    if ((status_reg & 0x80U) != 0U)
    {
        return;
    }

    adc_last_channel = (uint8_t)(status_reg & 0x0FU);
    if (adc_last_channel != 3U)
    {
        adc_read_errors++;
        return;
    }

    /*
     * Read DATA directly after the STATUS read. This matches the previous-PCB
     * debug harness and avoids doing a second STATUS read before DATA.
     */
    uint32_t raw = 0U;
    if (AD7124_ReadRegister(
            &AD7124_Handler,
            AD7124_DATA_REG,
            3U,
            &raw) != AD7124_OK)
    {
        adc_read_errors++;
        return;
    }
    adc_last_raw = raw;
    adc_samples_total++;

    /* Allow the digital filter to settle before collecting statistics. */
    if (noise_skip > 0U)
    {
        noise_skip--;
        return;
    }

    if (noise_n == 0U)
    {
        noise_min_acc = raw;
        noise_max_acc = raw;
        noise_mean_acc = 0.0;
        noise_m2_acc = 0.0;
    }
    if (raw < noise_min_acc)
    {
        noise_min_acc = raw;
    }
    if (raw > noise_max_acc)
    {
        noise_max_acc = raw;
    }

    /* Welford running mean and variance. */
    noise_n++;
    const double x = (double)raw;
    const double delta = x - noise_mean_acc;
    noise_mean_acc += delta / (double)noise_n;
    const double delta2 = x - noise_mean_acc;
    noise_m2_acc += delta * delta2;

    if (noise_n >= 2048U)
    {
        const double variance =
            noise_m2_acc / (double)(noise_n - 1U);
        const double std_codes = NoiseSqrt(variance);
        const double pp_codes =
            (double)(noise_max_acc - noise_min_acc);

        /*
         * CONFIG1 uses AVDD as Vref and gain 64.
         * For a bipolar code:
         *   Vin = ((code / 8388608) - 1) * Vref / gain
         *
         * AVDD is nominally 3.3 V, so the uV values are nominal
         * input-referred estimates. Raw-code statistics remain exact.
         */
        const double nominal_vref_V = 3.3;
        const double pga_gain = 64.0;
        const double code_to_uV =
            (nominal_vref_V / (pga_gain * 8388608.0)) * 1.0e6;
        noise_window_samples = noise_n;
        noise_raw_min = noise_min_acc;
        noise_raw_max = noise_max_acc;
        noise_mean_code = noise_mean_acc;
        noise_std_code  = std_codes;
        noise_pp_code   = pp_codes;
        noise_mean_uV =
            ((noise_mean_acc / 8388608.0) - 1.0) *
            (nominal_vref_V / pga_gain) *
            1.0e6;
        noise_std_uV = std_codes * code_to_uV;
        noise_pp_uV  = pp_codes  * code_to_uV;
        noise_done = 1U;

        /*
         * SECOND STOP:
         * Inspect noise_* plus adc_samples_total and adc_read_errors.
         */
    }
}

uint8_t Acquisition_IsComplete(void)
{
    return noise_done;
}
