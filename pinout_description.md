![Microchip Logo](assets/logo.png "Microchip Technology")

# Pinout Description — dsPIC33AK512MC510

**Pin and peripheral assignment reference for the MCLV-48V-300W + CAN FOC project**

![Device](https://img.shields.io/badge/device-dsPIC33AK512MC510-blue)
![Board](https://img.shields.io/badge/board-MCLV--48V--300W-blue)
![Updated](https://img.shields.io/badge/updated-2026--10--09-informational)

---

## Table of Contents

- [Named GPIO Pins](#named-gpio-pins)
- [ADC Channels](#adc-channels)
- [Analog Modules](#analog-modules-fixed-silicon-pins)
- [PWM Outputs](#pwm-outputs-fixed-silicon-pins)
- [Communication Peripherals](#communication-peripherals)
- [Analog Port Configuration](#analog-port-configuration-ansel-registers)
- [Source Files](#source-files)

---

## Named GPIO Pins

| Name | Pin | Direction | Function |
|------|-----|-----------|----------|
| `SW1` | **RA12** | Input | Push-button switch 1 |
| `SW2` | **RE1** | Input | Push-button switch 2 — FOC direction reversal |
| `LED1` | **RE2** | Output | User LED 1 |
| `LED2` | **RE3** | Output | User LED 2 |
| `HALL_A` | **RD4** | Input | Hall sensor phase A |
| `HALL_B` | **RD5** | Input | Hall sensor phase B |
| `HALL_C` | **RD6** | Input | Hall sensor phase C |
| `FLT_OC` | **RD7** | Input | Board overcurrent fault signal |
| `FLT_OC_OV` | **RD8** | Input | Board overcurrent/overvoltage fault signal |
| `FLT_LATCH` | **RE0** | Input | Board fault latch |

> Defined in: `mcc_generated_files/system/pins.h`

---

## ADC Channels

All three ADCs are configured as 12-bit with 4x oversampling (effective 13-bit), triggered by **PWM1 Trigger1**.

### ADC1 — Phase A Current + Motor Voltages

| Channel Name | ADC | ANx | Signal |
|---|---|---|---|
| `ADC1_IA` | ADC1 CH0 | **AD1AN0** | Phase A current (amplified via OPA1) |
| `ADC1_VM1` | ADC1 CH1 | **AD1AN3** | Motor voltage VM1 |
| `ADC1_VM2` | ADC1 CH2 | **AD1AN4** | Motor voltage VM2 |

### ADC2 — Phase B Current + Motor Voltage + Speed Reference

| Channel Name | ADC | ANx | Signal |
|---|---|---|---|
| `ADC2_IB` | ADC2 CH0 | **AD2AN0** | Phase B current (amplified via OPA2) |
| `ADC2_VM3` | ADC2 CH1 | **AD2AN4** | Motor voltage VM3 |
| `ADC2_POT` | ADC2 CH2 | **AD2AN5** | Speed reference potentiometer *(interrupt enabled → `MC_APP_ISR()`)* |

### ADC3 — DC Bus Current + Phase C Current + DC Bus Voltage

| Channel Name | ADC | ANx | Signal |
|---|---|---|---|
| `ADC3_IBUS` | ADC3 CH0 | **AD3AN0** | DC bus current |
| `ADC3_IC` | ADC3 CH1 | **AD3AN3** | Phase C current (amplified via OPA3) |
| `ADC3_VBUS` | ADC3 CH2 | **AD3AN4** | DC bus voltage |

> Defined in: `mcc_generated_files/adc/src/adc1.c`, `adc2.c`, `adc3.c`

---

## Analog Modules (Fixed Silicon Pins)

These modules use dedicated, non-remappable analog pins on the dsPIC33AK512MC510.

### On-Chip Operational Amplifiers (OPA)

| Module | Purpose |
|--------|---------|
| **OPA1** (AMP1) | Phase A current shunt amplifier — output feeds AD1AN0 |
| **OPA2** (AMP2) | Phase B current shunt amplifier — output feeds AD2AN0 |
| **OPA3** (AMP3) | Phase C current shunt amplifier — output feeds AD3AN3 |

All OPAs configured in high-power mode with NMOS/PMOS differential input pair.

> Defined in: `mcc_generated_files/opa/src/opa1.c`, `opa2.c`, `opa3.c`

### Comparator / DAC (CMP3 / DAC3) — Overcurrent Protection

**MCC-configured (CMP3_Initialize):**

| Parameter | Value | Description |
|---|---|---|
| Input pin | **CMP3A** | DC bus current signal (via OPA3) |
| `INPSEL` | 0 | Positive input = CMP3A |
| `INNSEL` | 0 | Negative input = DAC3 internal reference |
| `CMPPOL` | 0 | Non-inverted: output HIGH when overcurrent |
| `HYSSEL` | 3 | 45 mV hysteresis |
| `CBE` | 1 | Comparator blanking enabled |
| DAC threshold | 2900 counts | Corresponds to ~10 A fault threshold |
| `PGxF1PCI2` | `0x40000000` | Fault source = CMP3 output (bit 30) |

**HAL-configured (MC_HAL_OverCurrentProtectionInit):**

| Setting | Value | Description |
|---|---|---|
| Flash calibration | `CMP3_Calibrate()` | Factory trim from `0x7F20E0`: POSINLADJ / NEGINLADJ / DNLADJ + RREN |
| `TMCB` | 100 | Leading-edge blanking time (500 ns at 200 MHz Fp) |
| `PGxF1PCI1` | `0x03001000` | ACP[26:24]=3 latched, TERM[14:12]=1 cycle-by-cycle auto-terminate |

> MCC base config: `mcc_generated_files/cmp/src/cmp3.c`  
> HAL supplements: `src/hal/mc/mc_hal.c` (calibration, blanking time, PCI1 behavior)

---

## PWM Outputs (Fixed Silicon Pins)

Three high-speed PWM generators drive the 3-phase inverter in **complementary center-aligned** mode at **20 kHz** with **0.75 µs dead time**.

| Pin | Phase | Side |
|-----|-------|------|
| **PWM1H** | A | High-side |
| **PWM1L** | A | Low-side |
| **PWM2H** | B | High-side |
| **PWM2L** | B | Low-side |
| **PWM3H** | C | High-side |
| **PWM3L** | C | Low-side |

> Defined in: `mcc_generated_files/pwm_hs/src/pwm.c`

---

## Communication Peripherals

### UART1 (PPS-assigned)

| Signal | PPS Pin | Port | Config |
|--------|---------|------|--------|
| U1RX | RP47 | **RC14** | Input |
| U1TX | RP48 | **RC15** | Output (LAT initialized HIGH) |

115200 baud, 8-bit, 1 stop bit, no parity.

> Defined in: `mcc_generated_files/uart/src/uart1.c`

### CAN1 (PPS-assigned) — X2Cscope

| Signal | PPS Pin | Port | Direction | PPS Register |
|--------|---------|------|-----------|--------------|
| CAN1TX | RP44 | **RC11** | Output | `RPOR10bits.RP44R = 0x11` |
| CAN1RX | RP43 | **RC10** | Input | `RPINR17bits.CAN1RXR = 0x2B` |

Classic CAN 2.0, 500 kbit/s, standard 11-bit IDs, no BRS. Device -> PC ID `0x100` (TXQ), PC -> Device ID `0x110` (hardware filter 0 -> FIFO1 `X2CSCOPE_RX`).

RC11/RC10 are routed to the **XPRO1** header, where an **ATA6563 click** (MIKROE-2334) is attached through an Xplained Pro extension. The click needs 5 V in addition to 3.3 V: since XPRO1 only provides 3.3 V, 5 V is taken from the 5V pin of header **J5** and connected with a jumper wire to **5V_EXT** on the Xplained Pro extension.

### Clock Input

| Signal | Pin | Source |
|--------|-----|--------|
| OSCI | OSCI | 8 MHz MEMS oscillator (Y1) on the DIM module — External Clock, PLL1 reference (required for CAN timing accuracy) |

> PPS defined in: `mcc_generated_files/system/src/pins.c`  
> CAN driver: `mcc_generated_files/can/src/can1.c`

---

## Analog Port Configuration (ANSEL Registers)

Bits set to `1` in ANSEL configure the corresponding port pin as analog (disconnected from digital logic).

| Register | Value | Analog-mode pins |
|---|---|---|
| `ANSELA` | `0xEFFF` | RA0–RA11, RA13–RA15 (RA12 digital for SW1) |
| `ANSELB` | `0xFFFF` | RB0–RB15 |
| `ANSELC` | `0x33E5` | RC0, RC2, RC5–RC9, RC12–RC13 (RC10/RC11 digital for CAN1) |
| `ANSELD` | `0xFE00` | RD9–RD15 |
| `ANSELE` | `0x0000` | none |
| `ANSELF` | `0x0001` | RF0 |

> Defined in: `mcc_generated_files/system/src/pins.c`

---

## Source Files

| File | Content |
|------|---------|
| `mcc_generated_files/system/src/pins.c` | TRIS, LAT, ANSEL, PPS (UART1, CAN1) initialization |
| `mcc_generated_files/system/pins.h` | Named GPIO macros (SW1, SW2, LED1, LED2, HALL_A/B/C, FLT_*) |
| `mcc_generated_files/system/src/clock.c` | External 8 MHz clock, PLL1, clock generators (CLK10 = 20 MHz CAN clock) |
| `mcc_generated_files/adc/src/adc1.c` | ADC1: IA=AD1AN0, VM1=AD1AN3, VM2=AD1AN4 |
| `mcc_generated_files/adc/src/adc2.c` | ADC2: IB=AD2AN0, VM3=AD2AN4, POT=AD2AN5 (interrupt enabled) |
| `mcc_generated_files/adc/src/adc3.c` | ADC3: IBUS=AD3AN0, IC=AD3AN3, VBUS=AD3AN4 |
| `mcc_generated_files/pwm_hs/src/pwm.c` | PG1/PG2/PG3: complementary, center-aligned, 20 kHz |
| `mcc_generated_files/cmp/src/cmp3.c` | CMP3/DAC3 init (threshold, comparator, PCI2 source), calibration from `0x7F20E0` |
| `src/hal/mc/mc_hal.c` | OC protection: flash calibration, blanking time, PWM fault PCI1 behavior |
| `mcc_generated_files/opa/src/opa1.c` | OPA1 (Phase A shunt amplifier) |
| `mcc_generated_files/opa/src/opa2.c` | OPA2 (Phase B shunt amplifier) |
| `mcc_generated_files/opa/src/opa3.c` | OPA3 (Phase C shunt amplifier) |
| `mcc_generated_files/can/src/can1.c` | CAN1: 500 kbit/s, TXQ, FIFO1 `X2CSCOPE_RX`, filter 0 (reprogrammed by `X2CscopeComm.c`) |
| `mcc_generated_files/uart/src/uart1.c` | UART1: 115200 baud, 8N1 |

---

*Copyright © Microchip Technology Inc. All rights reserved.*
