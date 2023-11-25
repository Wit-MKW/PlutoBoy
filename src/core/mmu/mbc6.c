#include "mbc.h"
#include "mbc6.h"
#include "memory.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* Memory Bank Controller 6 
 * 1MB ROM 32KB RAM
 * 1MB flash
*/

static int cur_RAM_bankA = 0;
static int cur_RAM_bankB = 0;
static int cur_ROM_bankA = 0;
static int cur_ROM_bankB = 0;
static int ram_enabled = 0;
static int flash_enabled = 0;
static int flash_erase = 0;
static int flash_state = 0;

static int battery = 0;
static int sram_modified = 0;

static uint8_t *flash_banks;

void write_flash(uint32_t addr, uint8_t val) {
    static uint8_t data[0x80];
    static uint32_t prog_addr;
    static int last_written = 0;

    if (!(flash_enabled & 0x1) || addr >= 0x100000) return;

    if (addr == 0x5555 && val == 0xAA) {
        flash_state = (flash_state == 0x80 ? -0xAA : 0xAA);
    } else if (addr == 0x2AAA && val == 0x55) {
        flash_state = (flash_state == -0xAA ? -0x55 : flash_state == 0xAA ? 0x55 : 0);
    } else if (addr == 0x5555 && flash_state == 0x55) {
        switch (val) {
        case 0xA0:
            memset(data, 0xFF, sizeof(data));
            prog_addr = -1;
            last_written = 0;
        case 0x80: // fall-through
            flash_state = (flash_enabled == 0x2 ? val : 0);
            break;
        case 0xF0:
            if ((flash_enabled & 0x4) || (flash_erase & 0x1)) {
                flash_enabled &= 0x3;
                flash_erase &= 0x2;
            }
            break;
        case 0x90:
            flash_enabled |= 0x4;
        default: // fall-through
            flash_state = 0;
            break;
        }
    } else if (addr == 0x5555 && val == 0x10 && flash_state == -0x55) {
        memset(flash_banks, 0xFF, 0x100000);
        flash_erase |= 0x1;
        flash_state = 0;
    } else if ((addr & 0x1FFF) == 0) {
        if (flash_state == 0xA0) {
            if (prog_addr == -1) prog_addr = addr;
            if (prog_addr == addr) data[0] = val;
        } else {
            if (val == 0x30 && flash_state == -0x55) {
                memset(flash_banks + addr, 0xFF, 0x2000);
                flash_erase |= 0x2;
            } else if (val == 0xF0 && flash_state == 0x55) {
                flash_erase &= 0x1;
            }
            if (flash_state != 0x10) flash_state = 0;
        }
    } else if (flash_state == 0xA0) {
        if (prog_addr == -1) prog_addr = (addr & ~0x7F);
        if (prog_addr == (addr & ~0x7F)) {
            data[addr & 0x7F] = val;
            if ((addr & 0x7F) == 0x7F) {
                if (last_written && !val) {
                    for (uint32_t i = 0; i < 0x80; ++i) {
                        flash_banks[prog_addr + i] &= data[i];
                    }
                    flash_state = 0xF0;
                }
                last_written = 1;
            }
        }
    } else if (addr == prog_addr + 0x7F && val == 0xF0 && flash_state == 0xF0) {
        flash_state = 0;
    }
}

void setup_MBC6(int flags) {
    battery = (flags & BATTERY) ? 1 : 0;
    if (battery) {
        read_SRAM();
    }
    flash_banks = RAM_banks + RAM_bank_count * RAM_BANK_SIZE - 0x100000;
}

uint8_t read_MBC6(uint16_t addr) {
       
    switch (addr & 0xF000) {
     case 0x0000:
     case 0x1000:
     case 0x2000:
     case 0x3000: // Reading from fixed banks 0/1
                 return ROM_banks[addr];
                 break;
        
     case 0x4000:
     case 0x5000: // Reading from ROM/flash (A)
                 if (cur_ROM_bankA & 0x80) {
                    if (flash_erase || flash_state == 0xF0) {
                        return 0x80;
                    } else if (flash_state == 0xA0) {
                        return 0x00;
                    } else {
                        return flash_banks[(cur_ROM_bankA & 0x7F) << 13 | (addr & 0x1FFF)];
                    }
                 } else {
                    return ROM_banks[(cur_ROM_bankA << 13) | (addr & 0x1FFF)];
                 }
        
     case 0x6000:
     case 0x7000: // Reading from ROM/flash (B)
                 if (cur_ROM_bankB & 0x80) {
                    if (flash_erase || flash_state == 0xF0) {
                        return 0x80;
                    } else if (flash_state == 0xA0) {
                        return 0x00;
                    } else {
                        return flash_banks[(cur_ROM_bankB & 0x7F) << 13 | (addr & 0x1FFF)];
                    }
                 } else {
                    return ROM_banks[(cur_ROM_bankB << 13) | (addr & 0x1FFF)];
                 }
        
     case 0xA000: // Read from RAM (A)
                 return ram_enabled ? RAM_banks[(cur_RAM_bankA << 12) | (addr - 0xA000)] : 0xFF;
        
     case 0xB000: // Read from RAM (B)
                 return ram_enabled ? RAM_banks[(cur_RAM_bankB << 12) | (addr - 0xA000)] : 0xFF;
    };
    // Failed to read
    return 0x0;
}


void write_MBC6(uint16_t addr, uint8_t val) {
    
    switch (addr & 0xFC00) {
        case 0x0000: // Activate/Deactivate RAM banking
                    if (battery && ram_enabled && (val != 0xA) && sram_modified) {
                        write_SRAM();
                        sram_modified = 0;
                    }
                    ram_enabled = (val == 0xA);
                    break;
        case 0x0400: // Set current RAM bank (A)
                    cur_RAM_bankA = (val & 0x7);
                    break;
        case 0x0800: // Set current RAM bank (B)
                    cur_RAM_bankB = (val & 0x7);
                    break;
        case 0x0C00: // Enable/Disable flash
                    if (flash_enabled & 0x2) {
                        flash_enabled = (val & 0x1) | 0x2;
                    }
                    break;
        case 0x1000: // Enable/Disable flash writing
                    if (addr == 0x1000) {
                        flash_enabled = (val & 0x1) << 1 | (flash_enabled & 0x5);
                    }
                    break;
        case 0x2000:
        case 0x2400: // Set current ROM bank (A)
                    cur_ROM_bankA = (val & 0x7F) | (cur_ROM_bankA & 0x80);
                    break;
        case 0x2800:
        case 0x2C00: // Switch ROM/flash (A)
                    cur_ROM_bankA = (val & 0x1) << 7 | (cur_ROM_bankA & 0x7F);
                    break;
        case 0x3000:
        case 0x3400: // Set current ROM bank (B)
                    cur_ROM_bankB = (val & 0x7F) | (cur_ROM_bankB & 0x80);
                    break;
        case 0x3800:
        case 0x3C00: // Switch ROM/flash (B)
                    cur_ROM_bankB = (val & 0x1) << 7 | (cur_ROM_bankB & 0x7F);
                    break;
        case 0x4000:
        case 0x4400:
        case 0x4800:
        case 0x4C00:
        case 0x5000:
        case 0x5400:
        case 0x5800:
        case 0x5C00: // Write to flash (A)
                    if (cur_ROM_bankA & 0x80) {
                        write_flash((cur_ROM_bankA & 0x7F) << 13 | (addr & 0x1FFF), val);
                    }
                    break;
        case 0x6000:
        case 0x6400:
        case 0x6800:
        case 0x6C00:
        case 0x7000:
        case 0x7400:
        case 0x7800:
        case 0x7C00: // Write to flash (B)
                    if (cur_ROM_bankB & 0x80) {
                        write_flash((cur_ROM_bankB & 0x7F) << 13 | (addr & 0x1FFF), val);
                    }
                    break;
        case 0xA000:
        case 0xA400:
        case 0xA800:
        case 0xAC00: // Write to RAM (A)
                    if (ram_enabled && RAM_banks[(cur_RAM_bankA << 12) | (addr & 0x0FFF)] != val) {
                        RAM_banks[(cur_RAM_bankA << 12) | (addr & 0x0FFF)] = val;
                        sram_modified = 1;
                    }
                    break;
        case 0xB000:
        case 0xB400:
        case 0xB800:
        case 0xBC00: // Write to RAM (B)
                    if (ram_enabled && RAM_banks[(cur_RAM_bankB << 12) | (addr & 0x0FFF)] != val) {
                        RAM_banks[(cur_RAM_bankB << 12) | (addr & 0x0FFF)] = val;
                        sram_modified = 1;
                    }
                    break;
    }    
}
