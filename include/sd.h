#pragma once
#include "pff.h"
#include "diskio.h"

/*
page numbers come from:
SD Specifications Part 1
Physical Layer Simplified Specification
https://www.sdcard.org/downloads/pls/pdf/?p=Part1_Physical_Layer_Simplified_Specification_Ver9.10.jpg&f=Part1PhysicalLayerSimplifiedSpecificationVer9.10Fin_20231201.pdf&e=EN_SS9_1
*/

#define STA_INIT 0x00 /* Drive initialized */
#define SDHC 1
#define SDSC 0
#define BLOCK_SIZE 512
#define BUFFER_FLUSH_SIZE 10 // bufere size for flushing rest of line when exceed expected line length
#define FILE_NAME_LEN 13 // max file name length {8}.{3} + null char

/* Definitions for MMC/SDC command */
#define CMD0 (0x40 + 0)    /* GO_IDLE_STATE */
#define ACMD41 (0x40 + 41) /* SEND_OP_COND (SDC) */
#define CMD8 (0x40 + 8)    /* SEND_IF_COND */
#define CMD13 (0x40 + 13)  /* SD_STATUS */
#define CMD16 (0x40 + 16)  /* SET_BLOCKLEN */
#define CMD17 (0x40 + 17)  /* READ_SINGLE_BLOCK */
#define CMD24 (0x40 + 24)  /* WRITE_BLOCK */
#define CMD55 (0x40 + 55)  /* APP_CMD */
#define CMD58 (0x40 + 58)  /* READ_OCR */

#define ACMD41_ARG ((uint32_t)1 << 30) // HCS bit

#define PATTERN 0xAA
#define CMD8_ARG ((0b00001 << 8) | PATTERN)
// p.297
// [31:12] Reserved
// [11:8] Voltage supplied (VHS): 0b0001 2.7-3.6V
// [7:0]  check pattern

// R1 response codes (p.302)
typedef enum : uint8_t
{
    INITIALIZED = 0,
    IDLE_STATE = 1,
    WRITE_ACCEPTED = 5,
    ERROR = 100,
    DATA_START_TOKEN = 0xFE,
    NO_RESPONSE = 0xFF
} R1_RESP;

// R2 response with status register (p.303)
typedef enum : uint16_t
{
    // use to decode status.status_bits
    CARD_IS_LOCKED = 0x1,
    WP_ERASE_SKIP = 0x2,
    GENERAL_ERROR = 0x4,
    CC_ERRROR = 0x8,
    CARD_ECC_FAILED = 0x10,
    WP_VIOLATION = 0x20,
    ERASE_PARAM = 0x40,
    OUT_OF_RANGE = 0x80,
    IN_IDLE_STATE = 0x100,
    ERASE_RESET = 0x200,
    ILLEGAL_CMD = 0x400,
    COM_CRC_ERROR = 0x800,
    ERASE_SEQ_ERR = 0x1000,
    ADDR_ERROR = 0x2000,
    PARAMETER_ERROR = 0x4000,
} STATUS;

// response R3 voltages mask (OCR register) (p.223)
typedef enum : uint32_t
{
    VOLT_27_28 = 0x8000, // 15 bit
    VOLT_28_29 = 0x10000,
    VOLT_29_30 = 0x20000,
    VOLT_30_31 = 0x40000,
    VOLT_31_32 = 0x80000,
    VOLT_32_33 = 0x100000,
    VOLT_33_34 = 0x200000,
    VOLT_34_35 = 0x400000,
    VOLT_35_36 = 0x800000,  // 23 bit
    OCR_SDHC = 0x40000000,  // 30 bit, HIGH when SDHC
    OCR_STATUS = 0x80000000 // 31 bit, HIGH when ready
} OCR;

// card status info
typedef struct
{
    uint8_t no_init;
    uint8_t card_type;  // SDHC | SDSC
    FRESULT fat_result; // File function return code
    R1_RESP R1_msg;
    uint16_t status_bits;
    uint8_t last_CMD;
    uint32_t OCR;
} SD_info;

// rw result
typedef enum : uint8_t
{
    RW_OK = 0,
    RW_ERROR = 1,
    EOF
} RW_RESULT;

inline char opened_file[FILE_NAME_LEN] = {0}; // track opened file

void SD_init(void);
SD_info *get_SD_status();
void sd_writeln(char *buff, uint8_t buff_len, const char *file);
void sd_write_finalize(const char *file);
RW_RESULT sd_readln(char *buff, const char *file, uint16_t buffer_size);
RW_RESULT flush_line(void);
RW_RESULT read_buffer(char *buff, uint16_t buffer_size, const char *file);
RW_RESULT open_file(const char *file);
void check_file(const char *file, uint32_t *file_size);

/*---------------------------------------*/
/* helpers for disk control functions */
void SD_command(uint8_t cmd, uint32_t arg);
uint8_t CRC(uint8_t cmd, uint32_t arg);
R1_RESP SD_R1resp(void);
uint16_t SD_R2resp(void);
uint32_t SD_R7resp(void);
void delay_ms(uint8_t ms);