#include <avr/io.h>
#include <util/delay_basic.h>
#include "spi.h"
#include "sd.h"
#include "diskio.h"
#include "pff.h"
#include "strings.h"

static SD_info status;
static FATFS fsys;

SD_info *get_SD_status() { return &status; }

void SD_init()
{
    SPI_init(64); // init slowly
    status.fat_result = pf_mount(&fsys);
    SPI_init(); // clock div 16
}

void check_file(const char *file, uint32_t *file_size)
{
    if (status.no_init || status.fat_result != FR_OK)
        return;
    RW_RESULT open_result = open_file(file);
    if (open_result != RW_OK)
        *file_size = 0;
    *file_size = fsys.fsize;
}

void sd_write_finalize(const char *file)
{
    if (status.no_init || status.fat_result != FR_OK)
        return;
    UINT bw = 0; /* Pointer to number of bytes written */
    if (open_file(file) != RW_OK)
        return;
    status.fat_result = pf_write(0, 0, &bw);
}

void sd_writeln(char *str, uint8_t str_len, const char *file)
{
    /*
     * write str line to SD
     * buff must ends with 0x00 which is then replaced with EOL ('\n')
     */
    if (status.no_init || status.fat_result != FR_OK)
        return;

    DWORD offset;
    UINT bw = 0;         // number of bytes written
    str[str_len] = 0x0A; // EOL

    if (open_file(file) != RW_OK)
        return;

    status.fat_result = pf_write(str, str_len + 1, &bw);
    if (bw < (UINT)str_len + 1)
    // reached end of sector and petitFS finalized write and close file
    {
        offset = fsys.fptr;
        status.fat_result = pf_mount(&fsys);
        status.fat_result = pf_open(file);
        status.fat_result = pf_lseek(offset + (DWORD)str_len); // move to next sector
        // finish writing
        str_cut(str, str, bw, str_len + 1);
        status.fat_result = pf_write(str, (UINT)str_len + 1 - bw, &bw);
    }
}

/**
 * @brief Reads a line from a specified file into a buffer.
 *
 * This function attempts to read a line from a given file into the provided buffer,
 * reading only up to the specified buffer size. It ensures that the file pointer is
 * always set to the beginning of a new line after the end of line (EOL) character.
 * If no EOL is found within the buffer size, the function continues reading (flushing)
 * until an EOL is encountered and then sets the pointer at the beginning of the new line.
 * The function null-terminates the line in the buffer.
 *
 * @param buff Buffer to store the read data.
 * @param file File name from which to read the line.
 * @param buffer_size Number of bytes to read into the buffer.
 * @return RW_OK if a line is successfully read, RW_ERROR if an error occurs,
 *          or EOF if the end of file is reached.
 */
RW_RESULT sd_readln(char *buff, const char *file, uint16_t buffer_size)
{
    uint16_t n_pos;
    RW_RESULT rw_result;
    uint32_t start_pos = fsys.fptr;

    if (status.no_init || status.fat_result != FR_OK)
        return RW_ERROR;

    mem_set((uint8_t *)buff, buffer_size, 0x00); // zero the buffer

    rw_result = read_buffer(buff, buffer_size, file);
    if (rw_result != RW_OK)
        return rw_result;
    // check if new line within buff
    n_pos = str_find(buff, "\n");
    if (n_pos != 0)
    {
        // set file pointer to begining of new line
        FRESULT seek_result = pf_lseek(start_pos + n_pos + 1);
        status.fat_result = seek_result;
        // put null terminator at end of line
        if (buff[n_pos - 1] == '\r')
            buff[n_pos - 1] = 0x00; // windows
        else
            buff[n_pos] = 0x00; // linux}
        return RW_OK;
    };
    buff[buffer_size] = 0x00;
    return flush_line();
}

/**
 * Flush the rest of line until new line
 * and set position to begining of new line
 * use actually opened file
 *
 * @return RW_OK if EOL found, RW_ERROR or EOF otherwise
 */
RW_RESULT flush_line(void)
{
    char flush_line[BUFFER_FLUSH_SIZE];
    uint16_t n_pos;
    uint32_t start_pos = fsys.fptr;
    RW_RESULT rw_result;
    do
    {
        rw_result = read_buffer(flush_line, BUFFER_FLUSH_SIZE, opened_file);
        if (rw_result != RW_OK)
            return rw_result;
        n_pos = str_find(flush_line, "\n");
        if (n_pos != 0)
        {
            FRESULT seek_result = pf_lseek(start_pos + n_pos + 1);
            status.fat_result = seek_result;
            return RW_OK;
        }
        start_pos = fsys.fptr;
    } while (true);
}

/**
 * Read up to buffer_size bytes from file
 *
 * @param buff Pointer to buffer where to store read data
 * @param buffer_size Size of the buffer
 * @param file File name to read from
 * @return RW_OK if no errors, RW_ERROR if error, EOF if end of file
 */
RW_RESULT read_buffer(char *buff, uint16_t buffer_size, const char *file)
{
    UINT br = 0; /* Pointer to number of bytes read */

    if (open_file(file) != RW_OK)
        return RW_ERROR;

    FRESULT read_result = pf_read(buff, buffer_size, &br);
    status.fat_result = read_result;
    if (br == 0) // end of file
        return EOF;
    if (read_result != FR_OK)
        return RW_ERROR;
    return RW_OK;
}

/**
 * Check if file is already opened and open file if not
 *
 *
 * @brief Check if file is already opened and open file if not
 * @param file File name, must be less than 12 characters and null terminated
 * @return RW_OK if file is opened, RW_ERROR if not
 */
RW_RESULT open_file(const char *file)
{
    if (!str_compare(opened_file, file))
    {
        str_copy(opened_file, FILE_NAME_LEN, file);
        if ((status.fat_result = pf_open(file)) != FR_OK)
            return RW_ERROR;
    }
    return RW_OK;
}

// pages refer to "physical layer simplified specification version 9.1"
/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for Petit FatFs (C)ChaN, 2014      */
/*-----------------------------------------------------------------------*/

/*-----------------------------------------------------------------------*/
/* Initialize Disk Drive                                                 */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize(void)
{
    // function required by FatFs
    // returns byte:
    // STA_NOINIT		0x01	/* Drive not initialized */
    // STA_INIT (or anything else) is OK

    uint8_t count = 0;
    status.R1_msg = NO_RESPONSE;
    status.no_init = STA_NOINIT;

    delay_ms(10);
    // min of 74 clock cycles with CS high to synchronize. 8 clks per byte
    CS_HIGH; // disable chip
    for (uint8_t i = 0; i < 10; i++)
        SPI_transmit(0xFF);

    /* Select the card */
    CS_LOW;
    SPI_transmit(0xFF);

    // GO_IDLE_STATE - init card in spi mode with CS low
    SD_command(CMD0, 0);
    if (SD_R1resp() != IDLE_STATE)
        return STA_NOINIT;

    // CMD8 to make sure card is ver.2
    // will provide error R1 if not
    SD_command(CMD8, CMD8_ARG);
    if (SD_R1resp() != IDLE_STATE)
        return STA_NOINIT; // we care only about Ver2 or later
    if ((SD_R7resp() & 0xFF) != PATTERN)
        return STA_NOINIT;

    // up to 1sec for card to initialize
    do
    {
        delay_ms(10); // ACMD41 takes up to 1sec
        // CMD55: next cmd is application specific
        SD_command(CMD55, 0);
        if (SD_R1resp() == IDLE_STATE)
        {
            // ACMD41
            // Sends host capacity support information (HCS) and asks the
            // accessed card to send its operating condition register (OCR) content in the
            // response on the CMD line
            SD_command(ACMD41, ACMD41_ARG);
        }
        if (count++ > 200)
            return STA_NOINIT;
    } while (SD_R1resp() != INITIALIZED); // if initialized, no idle state (p.288)

    // CMD58 to know exactly the voltage and HCS bit
    SD_command(CMD58, 0);
    if (SD_R1resp() != INITIALIZED)
        return STA_NOINIT;
    uint32_t resp = SD_R7resp();
    status.OCR = resp;

    if (!(resp & (VOLT_29_30 | VOLT_30_31 | VOLT_31_32)))
        return STA_NOINIT;

    if (!(resp & OCR_STATUS)) // not ready if
        // if the card is SDHC: CMD8 not issued or ACMD41 with HCS=0 (p.287)
        return STA_NOINIT;

    status.card_type = (resp & OCR_SDHC) != 0; // SDHC=1 | SDSC=0
    if (status.card_type == SDSC)              // NOT TESTED!!!
    {
        SD_command(CMD16, BLOCK_SIZE); // set block size for SDSC card
        if (SD_R1resp() != INITIALIZED)
            return STA_NOINIT;
    }

    SPI_transmit(0xFF);
    CS_HIGH;
    SPI_transmit(0xFF);
    status.no_init = STA_INIT;
    return STA_INIT;
}

/*-----------------------------------------------------------------------*/
/* Read Partial Sector                                                   */
/*-----------------------------------------------------------------------*/

DRESULT disk_readp(
    BYTE *buff,   /* Pointer to the destination object */
    DWORD sector, /* Sector number (LBA) */
    UINT offset,  /* Offset in the sector */
    UINT count    /* Byte count (bit15:destination) */
)
{
    R1_RESP R1_resp;
    uint16_t remained = BLOCK_SIZE + 2 - offset - count; // remaining bytes and CRC
    uint16_t delay = 20;                                 // Time counter, minimum 100ms is required, give 200ms

    SPI_transmit(0xFF);
    CS_LOW;
    SPI_transmit(0xFF);

    if (status.card_type == SDSC)
        sector *= BLOCK_SIZE; /* Convert to byte address for SDSC cards */

    SD_command(CMD17, sector);
    do // Wait for data block
    {
        R1_resp = SD_R1resp();
        delay_ms(10);
        if (!delay--)
            return RES_ERROR;
    } while (R1_resp != DATA_START_TOKEN);

    // data arrived
    while (offset--) // skip leading bytes in the sector
        SPI_transmit(0xFF);

    do // store actual data
    {
        *buff++ = SPI_transmit(0xFF);
    } while (count--);

    /* Skip trailing bytes in the sector and block CRC */
    do
        SPI_transmit(0xFF);
    while (--remained);

    SPI_transmit(0xFF);
    CS_HIGH;
    SPI_transmit(0xFF);
    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Write Partial Sector                                                  */
/*-----------------------------------------------------------------------*/

DRESULT disk_writep(
    const BYTE *buff, /* Pointer to the data to be written, NULL:Initiate/Finalize write operation */
    DWORD sc          /* Sector number (LBA) or Number of bytes to send */
)
{
    DRESULT res = RES_ERROR;
    static uint16_t counter; // bytes counter

    SPI_transmit(0xFF);
    CS_LOW;
    SPI_transmit(0xFF);

    if (!buff)
    {
        if (sc) // Initiate write process
        {
            if (status.card_type == SDSC)
                sc *= BLOCK_SIZE;
            SD_command(CMD24, sc);
            if (SD_R1resp() == INITIALIZED)
            {
                SPI_transmit(DATA_START_TOKEN); // start data block token
                counter = BLOCK_SIZE;
                res = RES_OK;
            }
        }
        else // Finalize write process
        {
            sc = counter + 2;
            while (sc--)
                SPI_transmit(0x00); // Fill left bytes and CRC with zeros
            if ((SD_R1resp() & 0x1F) == WRITE_ACCEPTED)
            { // Receive data resp and wait for end of write process in timeout of 500ms
                counter = 50;
                do
                {
                    delay_ms(10);
                    if (SD_R1resp() == NO_RESPONSE)
                        return RES_OK;
                } while (counter--);
                res = RES_ERROR;
            }
        }
    }
    else // Send data to the disk
    {
        while (counter-- && sc--)
            SPI_transmit(*buff++);
        res = RES_OK;
    }

    SPI_transmit(0xFF);
    CS_HIGH;
    SPI_transmit(0xFF);

    return res;
}

/*-----------------------------------------------------------------------*/
/* helper SD functions                                                   */
/*-----------------------------------------------------------------------*/

void SD_command(uint8_t cmd, uint32_t arg)
{

    uint8_t crc = CRC(cmd, arg);

    // send command
    SPI_transmit(cmd);

    // send argument
    for (int8_t s = 24; s >= 0; s -= 8)
    {
        SPI_transmit(arg >> s);
    }

    // send CRC
    SPI_transmit(crc);

    // write last command for debug in case of failure
    if (cmd != CMD13)
        status.last_CMD = cmd - 0x40;
}

R1_RESP SD_R1resp(void)
{
    // first byte response
    R1_RESP resp;
    uint8_t i = 0;
    do
    {
        // In response to CMD0, the switching period is within 8 clocks
        // after the end bit of CMD0
        resp = (R1_RESP)SPI_transmit(0xFF);
        if (i++ >= 10)
            return NO_RESPONSE;
    } while (resp == NO_RESPONSE);

    if (resp != INITIALIZED &&
        resp != IDLE_STATE &&
        resp != DATA_START_TOKEN &&
        resp != NO_RESPONSE &&
        (resp & 0x1F) != WRITE_ACCEPTED)
    {
        status.R1_msg = ERROR;
        SD_command(CMD13, 0);
        SD_R2resp();
    }
    else
        status.R1_msg = resp;
    return resp;
}

uint16_t SD_R2resp(void)
{
    uint16_t resp = (uint16_t)SPI_transmit(0xFF) << 8 |
                    (uint16_t)SPI_transmit(0xFF);
    status.status_bits = resp;
    return resp;
}

uint32_t SD_R7resp(void)
{
    // second and rest byte response (p.304)
    // assume 1st byte already read with SD_R1resp()
    // [31-28] cmd version
    // [27-12] reserved
    // [11-8] voltage accepted
    // [7-0] check pattern
    uint32_t resp = (uint32_t)SPI_transmit(0xFF) << 24 |
                    (uint32_t)SPI_transmit(0xFF) << 16 |
                    (uint32_t)SPI_transmit(0xFF) << 8 |
                    (uint32_t)SPI_transmit(0xFF);
    return resp;
}

uint8_t CRC(uint8_t cmd, uint32_t arg)
{
    // SD card commands use a 7-bit CRC for error detection.
    // The CRC is calculated using the CRC-7 polynomial:
    // x^7 + x^3 + 1
    // which corresponds to the binary representation:
    // 0b10001001 (or 0x89 in hex).

    uint8_t crc = 0;
    // merge cmd & args
    uint8_t data[5];
    data[0] = cmd;
    data[1] = (arg >> 24) & 0xFF;
    data[2] = (arg >> 16) & 0xFF;
    data[3] = (arg >> 8) & 0xFF;
    data[4] = arg & 0xFF;

    for (int8_t byte = 0; byte < 5; byte++)
    {
        for (uint8_t bit = 0; bit < 8; bit++)
        {
            crc = crc << 1;
            if ((data[byte] & 0x80) ^ (crc & 0x80))
                crc = crc ^ 0x89;
            data[byte] = data[byte] << 1;
        }
    }
    crc = crc << 1;
    return (crc | 0x01); // last bit is always 1
}

void delay_ms(uint8_t ms)
{
    while (ms--)
        _delay_loop_2(F_CPU / 1000 / 4); // The loop executes four CPU cycles per iteration
}
