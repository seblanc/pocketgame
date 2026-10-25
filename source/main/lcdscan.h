#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "board.h"

/* Bit-banged ST7789 RDDID (04h) over the panel's bidirectional SDA line. It needs
 * no SPI peripheral, so it both verifies a known pin map and searches for an
 * unknown one. An ID of 0 or 0xFFFFFF means nothing answered. */

/* Reads the ID on p's pins (holds applied, RST pulsed, CS/DC low), twice when a
 * CS line lets the panel's interface be reset in between. Releases the pins. */
uint32_t lcdscan_read_id(const board_profile_t *p);

/* Searches cand[0..n) for the panel: every ordered pair is tried as SCLK/SDA
 * while the other idle_high pins sit at random levels, until one setting gets an
 * ID back; then each control line is flipped to tell CS, DC and RST apart. Only
 * idle_high pins are ever driven as control lines, all at the weakest drive.
 * Fills p's LCD pins and returns the verified ID, or 0. */
uint32_t lcdscan_search(const int *cand, const bool *idle_high, int n, board_profile_t *p);
