/**
 * @file    ad7124_config.c
 * @brief   AD7124-4 strain-only configuration for AxleSense PCB bring-up.
 *
 * Purpose:
 *   1) Reproduce the proven previous-PCB strain configuration.
 *   2) Disable the absent MEMS channels for a clean AIN4/AIN5 short noise test.
 *
 * PCB strain mapping:
 *   CH3 = AIN4(+) versus AIN5(-), Setup1
 *
 * Setup1:
 *   bipolar
 *   input buffers enabled
 *   AVDD reference
 *   PGA gain = 64
 *
 * Filter:
 *   FILTER1 = 0x460010, matching the previous PCB debug configuration.
 */

#include "ad7124.h"

AD7124_RegisterTypeDef configA = {
    /*
     * Full-power mode, continuous conversion, internal clock.
     * DATA_STATUS = 0 and CONT_READ = 0.
     */
    .adc_control = 0x0080U,

    /* Excitation currents and bias voltages disabled. */
    .io_control_1 = 0x000000U,
    .io_control_2 = 0x0000U,

    /* Enable SPI CRC error detection after initialization. */
    .error_enable = 0x000004U,

    /*
     * Only CH3 is enabled for this bring-up test.
     * 0x9085 = enable CH3, Setup1, AIN4(+) versus AIN5(-).
     */
    .channels = {
        0x0000U,
        0x0000U,
        0x0000U,
        0x9085U,
        0x0000U,
        0x0000U,
        0x0000U,
        0x0000U
    },

    /*
     * CONFIG1 = 0x087E:
     *   bipolar
     *   input buffers enabled
     *   AVDD reference
     *   PGA gain = 64
     *
     * CONFIG0 and unused setups retain benign previous values.
     */
    .configs = {
        0x0860U,
        0x087EU,
        0x0860U,
        0x0860U,
        0x0860U,
        0x0860U,
        0x0860U,
        0x0860U
    },

    /*
     * FILTER1 is the active strain filter.
     * Keep the previous PCB debug value exactly.
     */
    .filters = {
        0x460010U,
        0x460010U,
        0x060020U,
        0x060300U,
        0x060300U,
        0x060300U,
        0x060300U,
        0x060300U
    },

    .offsets = {
        0x800000U,
        0x800000U,
        0x800000U,
        0x800000U,
        0x800000U,
        0x800000U,
        0x800000U,
        0x800000U
    },

    .gains = {
        0x500000U,
        0x500000U,
        0x500000U,
        0x500000U,
        0x500000U,
        0x500000U,
        0x500000U,
        0x500000U
    }
};
