/*
 * TMC22xxUartInterface.cpp
 *
 *  Created on: 18 Sept 2022
 *      Author: David
 */

#include "TmcUartInterface.h"

#if RP2040

#include <hardware/pio.h>
#include <hardware/dma.h>
#include <hardware/structs/resets.h>			// for RESETS_RESET_PIO0_BITS
#include <hardware/structs/pio.h>				// for pio_hw_t
#include "PIOassignments.h"

constexpr unsigned int TmcClocksPerBit = 8;

static PIO pio_hw;
static uint8_t firstDmaChan;					// the first of two DMA channels
static unsigned int tmcProgramOffset;
static uint8_t tmcStateMachineNumber;

#define PICO_NO_HARDWARE	0					// used by output generated by pioasm

// rp2040 helper function to clear a hardware reset bit
static void rp2040_clear_reset(uint32_t reset_bit)
{
    if (resets_hw->reset & reset_bit)
    {
        hw_clear_bits(&resets_hw->reset, reset_bit);
        while (!(resets_hw->reset_done & reset_bit)) { }
    }
}

// PIO programs. See file TmcUartInterface.pio for the PIOASM source code.

// ------------- //
// TMC_Interface //
// ------------- //

#define TMC_Interface_wrap_target 11
#define TMC_Interface_wrap 17

static const uint16_t TMC_Interface_program_instructions[] = {
    0xe001, //  0: set    pins, 1
    0xe081, //  1: set    pindirs, 1
    0x80a0, //  2: pull   block
    0xa747, //  3: mov    y, osr                 [7]
    0x80a0, //  4: pull   block
    0xf727, //  5: set    x, 7            side 0 [7]
    0x6001, //  6: out    pins, 1
    0x0646, //  7: jmp    x--, 6                 [6]
    0xe601, //  8: set    pins, 1                [6]
    0x0084, //  9: jmp    y--, 4
    0xe380, // 10: set    pindirs, 0             [3]
            //     .wrap_target
    0x20a0, // 11: wait   1 pin, 0
    0x2020, // 12: wait   0 pin, 0
    0xe727, // 13: set    x, 7                   [7]
    0xa242, // 14: nop                           [2]
    0x4001, // 15: in     pins, 1
    0x064f, // 16: jmp    x--, 15                [6]
    0x8000, // 17: push   noblock
            //     .wrap
};

#if !PICO_NO_HARDWARE
static const struct pio_program TMC_Interface_program = {
    .instructions = TMC_Interface_program_instructions,
    .length = 18,
    .origin = -1,
};

static inline pio_sm_config TMC_Interface_program_get_default_config(uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset + TMC_Interface_wrap_target, offset + TMC_Interface_wrap);
    sm_config_set_sideset(&c, 2, true, false);
    return c;
}
#endif

static inline void uart_tx_program_init(PIO pio, uint sm, uint offset, uint pin_tx, uint baud) noexcept
{
	// Map PIO onto the TX/Rx pin with the IO mux
	pio_gpio_init(pio, pin_tx);
	pio_sm_config c = TMC_Interface_program_get_default_config(offset);
	// OUT shifts to right, no autopull
	sm_config_set_out_shift(&c, true, false, 32);
	// IN shifts to right, autopush disabled
	sm_config_set_in_shift(&c, true, false, 32);
	// We are mapping both OUT and side-set to the same pin, because sometimes we need to assert user data onto the pin (with OUT) and sometimes assert constant values (start/stop bit)
	sm_config_set_set_pins(&c, pin_tx, 1);		// for SET
	sm_config_set_out_pins(&c, pin_tx, 1);		// for OUT
	sm_config_set_sideset_pins(&c, pin_tx);		// for sideset
	sm_config_set_in_pins(&c, pin_tx);			// for WAIT, IN
	sm_config_set_jmp_pin(&c, pin_tx);			// for JMP
	// SM transmits 1 bit per 8 execution cycles.
	const float div = (float)SystemCoreClockFreq / (8 * baud);
	sm_config_set_clkdiv(&c, div);
	pio_sm_init(pio, sm, offset, &c);
}

// Public functions
// Initialise this interface. This must be called exactly once.
void TmcUartInterface::Init(Pin uartPin, uint32_t baudRate, uint8_t p_firstDmaChan) noexcept
{
	firstDmaChan = p_firstDmaChan;

	// Configure PIO clock
	pio_hw = (TmcUartPioNumber) ? pio1_hw : pio0_hw;
	const uint32_t rb = (TmcUartPioNumber) ? RESETS_RESET_PIO1_BITS : RESETS_RESET_PIO0_BITS;
	rp2040_clear_reset(rb);

    tmcProgramOffset = pio_add_program(pio_hw, &TMC_Interface_program);
    tmcStateMachineNumber = pio_claim_unused_sm(pio_hw, true);
    uart_tx_program_init(pio_hw, tmcStateMachineNumber, tmcProgramOffset, uartPin, baudRate);
}

// This is called before every transaction, so don't completely initialise everything
void TmcUartInterface::ResetUart() noexcept
{
	pio_sm_set_enabled(pio_hw, tmcStateMachineNumber, false);					// disable state machine
	pio_sm_clear_fifos(pio_hw, tmcStateMachineNumber);							// clear Tx and Rx fifos
	pio_sm_restart(pio_hw, tmcStateMachineNumber);								// reset the state
	pio_sm_exec(pio_hw, tmcStateMachineNumber, tmcProgramOffset);				// force state machine to start from the beginning of the program
}

// This is called to stop any pending DMA ready for reprogramming the DMAC
void TmcUartInterface::ResetDMA() noexcept
{
	DmacManager::DisableChannel(firstDmaChan);
	DmacManager::DisableChannel(firstDmaChan + 1);
}

// Set up the data to send and receive
void TmcUartInterface::SetDmaData(const volatile uint8_t* txData, unsigned int numTxBytes, volatile uint8_t* rxData, unsigned int numRxBytes) noexcept
{
	// Transmit
	dma_channel_config config = dma_channel_get_default_config(firstDmaChan);
	channel_config_set_read_increment(&config, true);
	channel_config_set_write_increment(&config, false);
	channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
	channel_config_set_dreq(&config, pio_get_dreq(pio_hw, tmcStateMachineNumber, true));
	dma_channel_configure(firstDmaChan, &config, &pio_hw->txf[tmcStateMachineNumber], txData, numTxBytes, false);
	pio_hw->txf[tmcStateMachineNumber] = numTxBytes - 1;							// put the byte count in the fifo

	// Receive
	config = dma_channel_get_default_config(firstDmaChan + 1);
	channel_config_set_read_increment(&config, false);
	channel_config_set_write_increment(&config, true);
	channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
	channel_config_set_dreq(&config, pio_get_dreq(pio_hw, tmcStateMachineNumber, false));
	dma_channel_configure(firstDmaChan + 1, &config, rxData, (const volatile uint8_t*)&pio_hw->rxf[tmcStateMachineNumber] + 3, numRxBytes, false);
}

// Start the send and receive and enable the DMA receive complete interrupt
void TmcUartInterface::StartTransfer(TmcUartCallbackFn callbackFn) noexcept
{
	DmacManager::SetInterruptCallback(firstDmaChan + 1, callbackFn, CallbackParameter(0));
	DmacManager::EnableCompletedInterrupt(firstDmaChan + 1);
	dma_start_channel_mask(3u << firstDmaChan);
	pio_sm_set_enabled(pio_hw, tmcStateMachineNumber, true);					// enable state machine
}

// Disable the DMA complete interrupt
void TmcUartInterface::DisableCompletedCallback() noexcept
{
	DmacManager::DisableCompletedInterrupt(firstDmaChan + 1);
}

void TmcUartInterface::AbortTransfer() noexcept
{
	pio_sm_set_enabled(pio_hw, tmcStateMachineNumber, false);					// disable state machine
	ResetDMA();
}

#endif

// End
