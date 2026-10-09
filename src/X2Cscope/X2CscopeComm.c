/*
Copyright (c) [2012-2020] Microchip Technology Inc.  
    All rights reserved.
    You are permitted to use the accompanying software and its derivatives 
    with Microchip products. See the Microchip license agreement accompanying 
    this software, if any, for additional info regarding your rights and 
    obligations.
    
    MICROCHIP SOFTWARE AND DOCUMENTATION ARE PROVIDED "AS IS" WITHOUT 
    WARRANTY OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT 
    LIMITATION, ANY WARRANTY OF MERCHANTABILITY, TITLE, NON-INFRINGEMENT 
    AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT WILL MICROCHIP OR ITS
    LICENSORS BE LIABLE OR OBLIGATED UNDER CONTRACT, NEGLIGENCE, STRICT 
    LIABILITY, CONTRIBUTION, BREACH OF WARRANTY, OR OTHER LEGAL EQUITABLE 
    THEORY FOR ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES INCLUDING BUT NOT 
    LIMITED TO ANY INCIDENTAL, SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES, 
    OR OTHER SIMILAR COSTS. 
*/
/*
 * This file is licensed according to the BSD 3-clause license as follows:
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the "Linz Center of Mechatronics GmbH" and "LCM" nor
 *       the names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL "Linz Center of Mechatronics GmbH" BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * This file is part of X2Cscope firmware implementation.
 * $LastChangedDate:: 2024-09-13 13:00:00 +0200#$
 */
/**
 * X2Cscope communication layer over CAN 2.0 (MCC Melody CAN1 driver).
 *
 * Based on X2Cscope_library interface/examples/can.
 *
 * Framing:
 *   X2Cscope uses a byte-stream protocol (LNet). CAN carries frames of up to
 *   8 bytes, so sendSerial() collects bytes and sends a full 8-byte frame every
 *   8 bytes. flushSerial() sends whatever is left at the end of each LNet frame.
 *   Received CAN frames are copied into a software ring buffer by
 *   X2CscopeComm_Communicate(); receiveSerial() drains it byte by byte.
 *
 * CAN IDs (standard 11-bit, classic CAN, no BRS):
 *   Device -> PC : X2CSCOPE_CAN_TX_ID (0x100)
 *   PC -> Device : X2CSCOPE_CAN_RX_ID (0x110)
 *
 * MCC requirements:
 *   - CAN1 RX FIFO with custom name X2CSCOPE_RX (checked at compile time).
 *   - Filter 0 routed to that FIFO. Its ID/mask are overwritten at post-init
 *     from X2CSCOPE_CAN_RX_ID, so this file is the single source of truth.
 *     Do not use filter 0 for anything else.
 *   - Hardware filtering guarantees X2CSCOPE_RX only holds X2CSCOPE_CAN_RX_ID
 *     frames, so no software ID check is done on reception.
 *
 * Usage: call X2CscopeComm_Communicate() from the main loop (instead of
 * X2Cscope_Communicate()) and X2Cscope_Update() from the fixed-rate ISR.
 */
#include <stdint.h>
#include <stdbool.h>

#include "X2CscopeComm.h"
#include "mcc_generated_files/can/can1.h"
#include "mcc_generated_files/can/can_types.h"

#include <xc.h>

/* MCC must provide an RX FIFO with custom name X2CSCOPE_RX */
#ifndef X2CSCOPE_RX
#error "X2Cscope: CAN1 RX FIFO custom name 'X2CSCOPE_RX' not found in can1.h - set it in MCC (CAN1 > FIFO custom name)"
#endif

/* CAN IDs used by X2Cscope (standard 11-bit). Single point of definition:
 * the RX acceptance filter is programmed from X2CSCOPE_CAN_RX_ID in
 * X2CscopeComm_PostInit(), so the MCC filter ID value is not relevant. */
#ifndef X2CSCOPE_CAN_TX_ID
#define X2CSCOPE_CAN_TX_ID  (0x100UL)   /* device -> PC (X2Cscope tool RX) */
#endif
#ifndef X2CSCOPE_CAN_RX_ID
#define X2CSCOPE_CAN_RX_ID  (0x110UL)   /* PC -> device (X2Cscope tool TX) */
#endif

#if (X2CSCOPE_CAN_RX_ID > 0x7FFUL) || (X2CSCOPE_CAN_TX_ID > 0x7FFUL)
#error "X2Cscope: CAN IDs must be standard 11-bit IDs (<= 0x7FF)"
#endif

/* -------------------------------------------------------------------------
 * RX ring buffer (power of 2, 8-bit indices wrap naturally).
 * Filled by X2CscopeComm_Communicate(), drained by receiveSerial().
 * ------------------------------------------------------------------------- */
#define RING_BITS   7u
#define RING_SZ     (1u << RING_BITS)   /* 128 bytes */
#define RING_MASK   (RING_SZ - 1u)

static volatile uint8_t rx_buf[RING_SZ];
static volatile uint8_t rx_head;        /* write index */
static volatile uint8_t rx_tail;        /* read  index */

/* -------------------------------------------------------------------------
 * TX accumulation buffer: one CAN frame worth of data.
 * ------------------------------------------------------------------------- */
#define CAN_MAX_DLC  8u

static uint8_t tx_buf[CAN_MAX_DLC];
static uint8_t tx_idx;

/* -------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */
static void ringbuf_reset(void)
{
    rx_head = 0u;
    rx_tail = 0u;
}

static uint8_t ringbuf_used(void)
{
    return (uint8_t)(rx_head - rx_tail);
}

static uint8_t ringbuf_free(void)
{
    return (uint8_t)(RING_SZ - ringbuf_used());
}

static void ringbuf_push(uint8_t byte)
{
    rx_buf[rx_head & RING_MASK] = byte;
    rx_head++;
}

static uint8_t ringbuf_pop(void)
{
    uint8_t byte = rx_buf[rx_tail & RING_MASK];
    rx_tail++;
    return byte;
}

static void can_transmit_frame(uint8_t len)
{
    struct CAN_MSG_OBJ msg = {0};
    uint8_t i;

    msg.msgId            = X2CSCOPE_CAN_TX_ID;
    msg.field.formatType = CAN_2_0_FORMAT;
    msg.field.brs        = CAN_NON_BRS_MODE;
    msg.field.frameType  = CAN_FRAME_DATA;
    msg.field.idType     = CAN_FRAME_STD;
    msg.field.dlc        = len;
    msg.data             = tx_buf;

    (void)CAN1_Transmit(CAN1_TXQ, &msg);

    for (i = 0u; i < CAN_MAX_DLC; i++) { tx_buf[i] = 0xFFu; }
}

/* -------------------------------------------------------------------------
 * X2Cscope communication callbacks
 * ------------------------------------------------------------------------- */

/**
 * @brief Accumulate one byte; a full 8-byte CAN frame is sent every 8 bytes.
 */
void sendSerial(uint8_t data)
{
    tx_buf[tx_idx & (CAN_MAX_DLC - 1u)] = data;

    if ((tx_idx & (CAN_MAX_DLC - 1u)) == (CAN_MAX_DLC - 1u))
    {
        can_transmit_frame(CAN_MAX_DLC);
    }

    tx_idx++;
}

/**
 * @brief Return one byte from the RX ring buffer.
 */
uint8_t receiveSerial(void)
{
    return ringbuf_pop();
}

/**
 * @brief true if at least one received byte is waiting.
 */
bool isReceiveDataAvailable(void)
{
    return (ringbuf_used() != 0u);
}

/**
 * @brief true if the CAN TX queue can accept a new frame.
 */
bool isSendReady(void)
{
    return (CAN_TX_FIFO_AVAILABLE == CAN1_TransmitFIFOStatusGet(CAN1_TXQ));
}

/**
 * @brief Send remaining bytes as a partial frame at the end of an LNet frame.
 */
void flushSerial(void)
{
    uint8_t remainder = tx_idx & (CAN_MAX_DLC - 1u);

    if (remainder != 0u)
    {
        can_transmit_frame(remainder);
    }

    tx_idx = 0u;
}

/**
 * @brief Drain the X2CSCOPE_RX FIFO into the ring buffer, then run the
 *        X2Cscope protocol engine. Call from the main loop.
 *
 * The hardware filter only lets X2CSCOPE_CAN_RX_ID into X2CSCOPE_RX, so every
 * frame is X2Cscope data. Frames are only taken while the ring buffer has room
 * for a full frame; the rest stay in the CAN FIFO until the next call.
 */
void X2CscopeComm_Communicate(void)
{
    struct CAN_MSG_OBJ rx_msg;
    uint8_t i;

    while ((ringbuf_free() >= CAN_MAX_DLC) && CAN1_ReceiveMessageGet(X2CSCOPE_RX, &rx_msg))
    {
        for (i = 0u; i < (uint8_t)rx_msg.field.dlc; i++)
        {
            ringbuf_push(rx_msg.data[i]);
        }
    }

    X2Cscope_Communicate();
}

/**
 * @brief Program CAN1 acceptance filter 0 to accept only X2CSCOPE_CAN_RX_ID
 *        (standard ID, exact match) into X2CSCOPE_RX.
 *
 * Filter registers may be written while the module is running, provided the
 * filter is disabled (FLTEN0 = 0) during the update. This overrides whatever
 * MCC configured for filter 0.
 */
static void can_rx_filter_config(void)
{
    C1FLTCON0bits.FLTEN0 = 0U;                      /* disable filter while updating */

    C1FLTOBJ0 = 0UL;
    C1FLTOBJ0bits.SID   = (uint16_t)(X2CSCOPE_CAN_RX_ID & 0x7FFUL);
    C1FLTOBJ0bits.EXIDE = 0U;                       /* standard frame */

    C1MASK0 = 0UL;
    C1MASK0bits.MSID = 0x7FFU;                      /* compare all 11 SID bits */
    C1MASK0bits.MIDE = 1U;                          /* match only standard IDs */

    C1FLTCON0bits.F0BP   = (uint8_t)X2CSCOPE_RX;
    C1FLTCON0bits.FLTEN0 = 1U;                      /* enable filter */
}

/**
 * @brief Comm-layer post-init, called from X2Cscope_Init().
 */
void X2CscopeComm_PostInit(void)
{
    uint8_t i;

    can_rx_filter_config();
    ringbuf_reset();
    tx_idx = 0u;
    for (i = 0u; i < CAN_MAX_DLC; i++) { tx_buf[i] = 0xFFu; }
}

/* *****************************************************************************
 End of File
 */