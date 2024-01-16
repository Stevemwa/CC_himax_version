/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "WE2_device.h"
#include "board.h"
#include "hx_drv_spi.h"
#include "hx_drv_gpio.h"
#include "hx_drv_scu_export.h"
#include "hx_drv_scu.h"

#include "ff.h"     /* Obtains integer types */
#include "diskio.h" /* Declarations of disk functions */
#include "mmc_we2.h"

#define ASSERT_HIGH(X) assert(X == ARM_DRIVER_OK)

#define MAX_FREQ (24000000)

static DEV_SPI_PTR spi = NULL;
static uint32_t spi_freq = MAX_FREQ;
volatile static bool busy = false;

/* MMC/SD command */
#define CMD0 (0)           /* GO_IDLE_STATE */
#define CMD1 (1)           /* SEND_OP_COND (MMC) */
#define ACMD41 (0x80 + 41) /* SEND_OP_COND (SDC) */
#define CMD8 (8)           /* SEND_IF_COND */
#define CMD9 (9)           /* SEND_CSD */
#define CMD10 (10)         /* SEND_CID */
#define CMD12 (12)         /* STOP_TRANSMISSION */
#define ACMD13 (0x80 + 13) /* SD_STATUS (SDC) */
#define CMD16 (16)         /* SET_BLOCKLEN */
#define CMD17 (17)         /* READ_SINGLE_BLOCK */
#define CMD18 (18)         /* READ_MULTIPLE_BLOCK */
#define CMD23 (23)         /* SET_BLOCK_COUNT (MMC) */
#define ACMD23 (0x80 + 23) /* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24 (24)         /* WRITE_BLOCK */
#define CMD25 (25)         /* WRITE_MULTIPLE_BLOCK */
#define CMD32 (32)         /* ERASE_ER_BLK_START */
#define CMD33 (33)         /* ERASE_ER_BLK_END */
#define CMD38 (38)         /* ERASE */
#define CMD48 (48)         /* READ_EXTR_SINGLE */
#define CMD49 (49)         /* WRITE_EXTR_SINGLE */
#define CMD55 (55)         /* APP_CMD */
#define CMD58 (58)         /* READ_OCR */

static volatile DSTATUS Stat = STA_NOINIT; /* Physical drive status */
// static volatile UINT Timer1, Timer2;        /* 1kHz decrement timer stopped at zero (disk_timerproc()) */

static uint8_t CardType; /* Card type flags */

// #define FCLK_SLOW()                                             \
//     do                                                          \
//     {                                                           \
//         spi_freq = 400000;                                      \
//         spi->spi_control(SPI_CMD_MST_UPDATE_SYSCLK, &spi_freq); \
//     } while (0)
// #define FCLK_FAST()                                             \
//     do                                                          \
//     {                                                           \
//         spi_freq = MAX_FREQ;                                    \
//         spi->spi_control(SPI_CMD_MST_UPDATE_SYSCLK, &spi_freq); \
//     } while (0)

#define FCLK_SLOW()
#define FCLK_FAST()

/*-----------------------------------------------------------------------*/
/* SPI controls (Platform dependent)                                     */
/*-----------------------------------------------------------------------*/
#define MMC_WP() 0
#define MMC_CD() 1
#define CS_HIGH() hx_drv_gpio_set_out_value(GPIO16, 1)
#define CS_LOW() hx_drv_gpio_set_out_value(GPIO16, 0)

static void spi_callback_fun_tx(void *arg)
{
    busy = false;
}

static void spi_callback_fun_rx(void *arg)
{
    busy = false;
}

static void spi_write(DEV_SPI_PTR spi, uint8_t *data, uint32_t size)
{
    while (busy)
    {
        board_delay_ms(1);
    }
    busy = true;
    spi->spi_write(data, size);
    while (busy)
    {
        board_delay_ms(1);
    }
}

static void spi_read(DEV_SPI_PTR spi, uint8_t *data, uint32_t size)
{
    while (busy)
    {
        board_delay_ms(1);
    }

    busy = true;
    spi->spi_read(data, size);
    while (busy)
    {
        board_delay_ms(1);
    }
}

static void mmc_init(void)
{
    hx_drv_scu_set_PB2_pinmux(SCU_PB2_PINMUX_SPI_M_DO_1);
    hx_drv_scu_set_PB3_pinmux(SCU_PB3_PINMUX_SPI_M_DI_1);
    hx_drv_scu_set_PB4_pinmux(SCU_PB4_PINMUX_SPI_M_SCLK_1);
    hx_drv_scu_set_PB5_pinmux(SCU_PB5_PINMUX_GPIO16);

    SCU_PAD_PULL_LIST_T pad_pull_cfg;

    hx_drv_scu_get_all_pull_cfg(&pad_pull_cfg);

    pad_pull_cfg.pb2.pull_en = SCU_PAD_PULL_EN;

    pad_pull_cfg.pb2.pull_sel = SCU_PAD_PULL_UP;

    pad_pull_cfg.pb3.pull_en = SCU_PAD_PULL_EN;

    pad_pull_cfg.pb3.pull_sel = SCU_PAD_PULL_UP;

    pad_pull_cfg.pb5.pull_en = SCU_PAD_PULL_EN;

    pad_pull_cfg.pb5.pull_sel = SCU_PAD_PULL_UP;

    hx_drv_scu_set_all_pull_cfg(&pad_pull_cfg);

    hx_drv_gpio_set_output(GPIO16, 1);

    hx_drv_spi_mst_init(USE_DW_SPI_MST_S, DW_SPI_S_RELBASE);

    spi = hx_drv_spi_mst_get_dev(USE_DW_SPI_MST_S);
}

/* Initialize MMC interface */
static void power_on(void)
{
    spi->spi_open(DEV_MASTER_MODE, MAX_FREQ);

    spi->spi_control(SPI_CMD_SET_TXCB, (SPI_CTRL_PARAM)spi_callback_fun_tx);
    spi->spi_control(SPI_CMD_SET_RXCB, (SPI_CTRL_PARAM)spi_callback_fun_rx);

    CS_HIGH();          /* Set CS# high */
    board_delay_ms(10); /* 10ms */
}

static void power_off(void)
{
    CS_HIGH(); /* Set CS# high */
    spi->spi_control(SPI_CMD_SET_TXCB, (SPI_CTRL_PARAM)NULL);
    spi->spi_control(SPI_CMD_SET_RXCB, (SPI_CTRL_PARAM)NULL);
    spi->spi_close();
}

/* Exchange a uint8_t */
static uint8_t xchg_spi(
    uint8_t dat /* Data to send */
)
{
    uint8_t dataout = dat;
    uint8_t datain = 0x00;
    if (dataout == 0xFF)
    {
        spi_read(spi, &datain, 1);
    }
    else
    {
        spi_write(spi, &dataout, 1);
    }
    return datain;
}

/* Receive multiple uint8_t */
static void rcvr_spi_multi(
    uint8_t *buff, /* Pointer to data buffer */
    UINT btr       /* Number of uint8_ts to receive (even number) */
)
{
    uint32_t block_num = btr / 512;
    uint32_t last_btr = btr - (block_num * 512);

    while (block_num)
    {
        spi_read(spi, buff, 512);
        block_num--;
        buff += 512;
    }

    if (last_btr)
    {
        spi_read(spi, buff, last_btr);
    }
}

#if FF_FS_READONLY == 0
/* Send multiple uint8_t */
static void xmit_spi_multi(
    const uint8_t *buff, /* Pointer to the data */
    UINT btx             /* Number of uint8_ts to send (even number) */
)
{
    uint32_t block_num = btx / 512;
    uint32_t last_btx = btx - (block_num * 512);

    while (block_num)
    {
        spi_write(spi, buff, 512);
        block_num--;
        buff += 512;
    }

    if (last_btx)
    {
        spi_write(spi, buff, last_btx);
    }
}
#endif

/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/
static int wait_ready(        /* 1:Ready, 0:Timeout */
                      UINT wt /* Timeout [ms] */
)
{
    uint8_t datain;
    uint32_t cnt = 0;

    datain = xchg_spi(0xFF);
    while (datain != 0xFF && cnt <= wt)
    {
        datain = xchg_spi(0xFF);
        board_delay_ms(1);
        cnt++;
        /* This loop will take a time. Insert rot_rdq() here for multitask envilonment. */
    } /* Wait for card goes ready or timeout */

    return (datain == 0xFF) ? 1 : 0;
}

/*-----------------------------------------------------------------------*/
/* Deselect card and release SPI                                         */
/*-----------------------------------------------------------------------*/
static void deselect(void)
{
    CS_HIGH();      /* Set CS# high */
    xchg_spi(0xFF); /* Dummy clock (force DO hi-z for multiple slave SPI) */
}

/*-----------------------------------------------------------------------*/
/* Select card and wait for ready                                        */
/*-----------------------------------------------------------------------*/
static int select(void) /* 1:OK, 0:Timeout */
{
    CS_LOW();       /* Set CS# low */
    xchg_spi(0xFF); /* Dummy clock (force DO enabled) */

    if (wait_ready(500))
        return 1; /* Wait for card ready */

    deselect();
    return 0; /* Timeout */
}

/*-----------------------------------------------------------------------*/
/* Receive a data packet from the MMC                                    */
/*-----------------------------------------------------------------------*/
static int rcvr_datablock(               /* 1:OK, 0:Error */
                          uint8_t *buff, /* Data buffer */
                          UINT btr       /* Data block length (uint8_t) */
)
{
    uint8_t token;
    uint32_t cnt = 0;
    uint32_t wt_ms = 200;

    token = xchg_spi(0xFF);

    while ((token == 0xFF) && cnt <= wt_ms)
    { /* Wait for DataStart token in timeout of 200ms */
        token = xchg_spi(0xFF);
        board_delay_ms(1);
        cnt++;
        /* This loop will take a time. Insert rot_rdq() here for multitask envilonment. */
    }

    if (token != 0xFE)
        return 0; /* Function fails if invalid DataStart token or timeout */

    rcvr_spi_multi(buff, btr); /* Store trailing data to the buffer */
    xchg_spi(0xFF);
    xchg_spi(0xFF); /* Discard CRC */

    return 1; /* Function succeeded */
}

/*-----------------------------------------------------------------------*/
/* Send a data packet to the MMC                                         */
/*-----------------------------------------------------------------------*/
#if FF_FS_READONLY == 0
static int xmit_datablock(                     /* 1:OK, 0:Failed */
                          const uint8_t *buff, /* Ponter to 512 uint8_t data to be sent */
                          uint8_t token        /* Token */
)
{
    uint8_t resp;

    if (!wait_ready(500))
        return 0; /* Wait for card ready */

    xchg_spi(token); /* Send token */

    if (token != 0xFD)
    {                              /* Send data if token is other than StopTran */
        xmit_spi_multi(buff, 512); /* Data */
        xchg_spi(0xFF);
        xchg_spi(0xFF);        /* Dummy CRC */
        resp = xchg_spi(0xFF); /* Receive data resp */

        if ((resp & 0x1F) != 0x05)
            return 0; /* Function fails if the data packet was not accepted */
    }

    return 1;
}
#endif

/*-----------------------------------------------------------------------*/
/* Send a command packet to the MMC                                      */
/*-----------------------------------------------------------------------*/
static uint8_t send_cmd(             /* Return value: R1 resp (bit7==1:Failed to send) */
                        uint8_t cmd, /* Command index */
                        DWORD arg    /* Argument */
)
{
    uint8_t n, res;

    if (cmd & 0x80)
    { /* Send a CMD55 prior to ACMD<n> */
        cmd &= 0x7F;
        res = send_cmd(CMD55, 0);

        if (res > 1)
            return res;
    }

    /* Select the card and wait for ready except to stop multiple block read */
    if (cmd != CMD12)
    {
        deselect();

        if (!select())
            return 0xFF;
    }

    /* Send command packet */
    xchg_spi(0x40 | cmd);        /* Start + command index */
    xchg_spi((BYTE)(arg >> 24)); /* Argument[31..24] */
    xchg_spi((BYTE)(arg >> 16)); /* Argument[23..16] */
    xchg_spi((BYTE)(arg >> 8));  /* Argument[15..8] */
    xchg_spi((BYTE)arg);         /* Argument[7..0] */
    n = 0x01;                    /* Dummy CRC + Stop */

    if (cmd == CMD0)
        n = 0x95; /* Valid CRC for CMD0(0) */

    if (cmd == CMD8)
        n = 0x87; /* Valid CRC for CMD8(0x1AA) */

    xchg_spi(n);

    /* Receive command resp */
    if (cmd == CMD12)
        xchg_spi(0xFF); /* Diacard following one byte when CMD12 */

    n = 10; /* Wait for response (10 bytes max) */

    do
    {
        res = xchg_spi(0xFF);
    } while ((res & 0x80) && --n);

    return res; /* Return received response */
}

/*--------------------------------------------------------------------------

   Public Functions

---------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------*/
/* Initialize disk drive                                                 */
/*-----------------------------------------------------------------------*/
DSTATUS mmc_disk_initialize(void)
{
    uint8_t n, cmd, ty, ocr[4];

    mmc_init(); /* Initialize SPI */

    for (int i = 0; i < 30; i++)
    {
        power_off(); /* Turn off the socket power to reset the card */

        if (MMC_WP())
        { /* Write protected */
            Stat |= STA_PROTECT;
        }
        else
        { /* Write enabled */
            Stat &= ~STA_PROTECT;
        }

        if (MMC_CD())
        { /* Card is in socket */
            Stat &= ~STA_NODISK;
        }
        else
        { /* Socket empty */
            Stat |= (STA_NODISK | STA_NOINIT);
        }

        if (Stat & STA_NODISK)
            return Stat; /* Is card existing in the soket? */

        power_on(); /* Turn on the socket power */
        FCLK_SLOW();

        for (n = 20; n; n--)
            xchg_spi(0xFF); /* Send 80 dummy clocks */

        ty = 0;

        if (send_cmd(CMD0, 0) == 1)
        {                          /* Put the card SPI/Idle state */
            uint32_t wt_ms = 1000; /* Initialization timeout = 1 sec */
            uint32_t cnt = 0;

            if (send_cmd(CMD8, 0x1AA) == 1)
            { /* SDv2? */
                for (n = 0; n < 4; n++)
                    ocr[n] = xchg_spi(0xFF); /* Get 32 bit return value of R7 resp */

                if (ocr[2] == 0x01 && ocr[3] == 0xAA)
                { /* Is the card supports vcc of 2.7-3.6V? */

                    while (cnt <= wt_ms && send_cmd(ACMD41, 1UL << 30))
                    { /* Wait for end of initialization with ACMD41(HCS) */
                        board_delay_ms(1);
                        cnt++;
                    }

                    if (cnt <= wt_ms && send_cmd(CMD58, 0) == 0)
                    { /* Check CCS bit in the OCR */
                        for (n = 0; n < 4; n++)
                            ocr[n] = xchg_spi(0xFF);

                        ty = (ocr[0] & 0x40) ? CT_SDC2 | CT_BLOCK : CT_SDC2; /* Card id SDv2 */
                    }
                }
            }
            else
            { /* Not SDv2 card */
                if (send_cmd(ACMD41, 0) <= 1)
                { /* SDv1 or MMC? */
                    ty = CT_SDC1;
                    cmd = ACMD41; /* SDv1 (ACMD41(0)) */
                }
                else
                {
                    ty = CT_MMC3;
                    cmd = CMD1; /* MMCv3 (CMD1(0)) */
                }

                while (cnt <= wt_ms && send_cmd(cmd, 0))
                { /* Wait for end of initialization */
                    board_delay_ms(1);
                    cnt++;
                }

                if (cnt > wt_ms || send_cmd(CMD16, 512) != 0) /* Set block length: 512 */
                    ty = 0;
            }
        }

        CardType = ty; /* Card type */
        deselect();

        if (ty)
        {                        /* OK */
            FCLK_FAST();         /* Set fast clock */
            Stat &= ~STA_NOINIT; /* Clear STA_NOINIT flag */
        }
        else
        { /* Failed */
            Stat = STA_NOINIT;
            power_off();
        }
        if (Stat != STA_NOINIT)
        {
            break;
        }
    }

    return Stat;
}

/*-----------------------------------------------------------------------*/
/* Get disk status                                                       */
/*-----------------------------------------------------------------------*/
DSTATUS mmc_disk_status(void)
{
    return Stat; /* Return disk status */
}

/*-----------------------------------------------------------------------*/
/* Read sector(s)                                                        */
/*-----------------------------------------------------------------------*/
DRESULT mmc_disk_read(
    uint8_t *buff, /* Pointer to the data buffer to store read data */
    LBA_t sector,  /* Start sector number (LBA) */
    UINT count     /* Sector count (1..128) */
)
{
    DWORD sect = (DWORD)sector;

    if (!count)
        return RES_PARERR; /* Check parameter */

    if (Stat & STA_NOINIT)
        return RES_NOTRDY; /* Check if drive is ready */

    if (!(CardType & CT_BLOCK))
        sect *= 512; /* LBA ot BA conversion (uint8_t addressing cards) */

    if (count == 1)
    {                                    /* Single sector read */
        if ((send_cmd(CMD17, sect) == 0) /* READ_SINGLE_BLOCK */
            && rcvr_datablock(buff, 512))
        {
            count = 0;
        }
    }
    else
    { /* Multiple sector read */
        if (send_cmd(CMD18, sect) == 0)
        { /* READ_MULTIPLE_BLOCK */
            do
            {
                if (!rcvr_datablock(buff, 512))
                    break;
                buff += 512;
            } while (--count);

            send_cmd(CMD12, 0); /* STOP_TRANSMISSION */
        }
    }

    deselect();
    return count ? RES_ERROR : RES_OK; /* Return result */
}

/*-----------------------------------------------------------------------*/
/* Write sector(s)                                                       */
/*-----------------------------------------------------------------------*/
#if FF_FS_READONLY == 0
DRESULT mmc_disk_write(
    const uint8_t *buff, /* Pointer to the data to be written */
    LBA_t sector,        /* Start sector number (LBA) */
    UINT count           /* Sector count (1..128) */
)
{
    DWORD sect = (DWORD)sector;

    if (!count)
        return RES_PARERR; /* Check parameter */

    if (Stat & STA_NOINIT)
        return RES_NOTRDY; /* Check drive status */

    if (Stat & STA_PROTECT)
        return RES_WRPRT; /* Check write protect */

    if (!(CardType & CT_BLOCK))
        sect *= 512; /* LBA ==> BA conversion (uint8_t addressing cards) */

    if (count == 1)
    {                                    /* Single sector write */
        if ((send_cmd(CMD24, sect) == 0) /* WRITE_BLOCK */
            && xmit_datablock(buff, 0xFE))
        {
            count = 0;
        }
    }
    else
    { /* Multiple sector write */
        if (CardType & CT_SDC)
            send_cmd(ACMD23, count); /* Predefine number of sectors */

        if (send_cmd(CMD25, sect) == 0)
        { /* WRITE_MULTIPLE_BLOCK */
            do
            {
                if (!xmit_datablock(buff, 0xFC))
                    break;

                buff += 512;
            } while (--count);

            if (!xmit_datablock(0, 0xFD))
                count = 1; /* STOP_TRAN token */
        }
    }

    deselect();
    return count ? RES_ERROR : RES_OK; /* Return result */
}
#endif

/*-----------------------------------------------------------------------*/
/* Miscellaneous drive controls other than data read/write               */
/*-----------------------------------------------------------------------*/
DRESULT mmc_disk_ioctl(
    uint8_t cmd, /* Control code */
    void *buff   /* Buffer to send/receive control data */
)
{
    DRESULT res;
    uint8_t n, csd[16];
    // DWORD st, ed;
    DWORD csize;
    LBA_t *dp;
#if _USE_ISDIO
    SDIO_CTRL *sdi;
    uint8_t rc, *bp;
    UINT dc;
    uint32_t wt_ms = 1000; /* Initialization timeout = 1 sec */
    uint32_t cnt = 0;
#endif

    if (Stat & STA_NOINIT)
        return RES_NOTRDY; /* Check if drive is ready */

    res = RES_ERROR;

    switch (cmd)
    {
    case CTRL_SYNC: /* Wait for end of internal write process of the drive */
        if (select())
            res = RES_OK;
        break;

    case GET_SECTOR_COUNT: /* Get drive capacity in unit of sector (DWORD) */
        if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16))
        {
            if ((csd[0] >> 6) == 1)
            { /* SDC CSD ver 2 */
                csize = csd[9] + ((WORD)csd[8] << 8) + ((DWORD)(csd[7] & 63) << 16) + 1;
                *(LBA_t *)buff = csize << 10;
            }
            else
            { /* SDC CSD ver 1 or MMC */
                n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
                csize = (csd[8] >> 6) + ((WORD)csd[7] << 2) + ((WORD)(csd[6] & 3) << 10) + 1;
                *(LBA_t *)buff = csize << (n - 9);
            }
            res = RES_OK;
        }
        break;

    case GET_BLOCK_SIZE: /* Get erase block size in unit of sector (DWORD) */
        if (CardType & CT_SDC2)
        { /* SDC ver 2+ */
            if (send_cmd(ACMD13, 0) == 0)
            { /* Read SD status */
                xchg_spi(0xFF);

                if (rcvr_datablock(csd, 16))
                { /* Read partial block */
                    for (n = 64 - 16; n; n--)
                        xchg_spi(0xFF); /* Purge trailing data */

                    *(DWORD *)buff = 16UL << (csd[10] >> 4);
                    res = RES_OK;
                }
            }
        }
        else
        { /* SDC ver 1 or MMC */
            if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16))
            { /* Read CSD */
                if (CardType & CT_SDC1)
                { /* SDC ver 1.XX */
                    *(DWORD *)buff = (((csd[10] & 63) << 1) + ((WORD)(csd[11] & 128) >> 7) + 1) << ((csd[13] >> 6) - 1);
                }
                else
                { /* MMC */
                    *(DWORD *)buff = ((WORD)((csd[10] & 124) >> 2) + 1) * (((csd[11] & 3) << 3) + ((csd[11] & 224) >> 5) + 1);
                }
                res = RES_OK;
            }
        }
        break;
#if FF_USE_TRIM
    case CTRL_TRIM: /* Erase a block of sectors (used when _USE_ERASE == 1) */
        if (!(CardType & CT_SDC))
            break; /* Check if the card is SDC */

        if (mmc_disk_ioctl(MMC_GET_CSD, csd))
            break; /* Get CSD */

        if (!(csd[10] & 0x40))
            break; /* Check if ERASE_BLK_EN = 1 */

        dp = buff;
        st = (DWORD)dp[0];
        ed = (DWORD)dp[1]; /* Load sector block */

        if (!(CardType & CT_BLOCK))
        {
            st *= 512;
            ed *= 512;
        }

        if (send_cmd(CMD32, st) == 0 && send_cmd(CMD33, ed) == 0 && send_cmd(CMD38, 0) == 0 && wait_ready(30000))
        {                 /* Erase sector block */
            res = RES_OK; /* FatFs does not check result of this command */
        }

        break;
#endif
        /* Following commands are never used by FatFs module */

    case MMC_GET_TYPE: /* Get MMC/SDC type (uint8_t) */
        *(uint8_t *)buff = CardType;
        res = RES_OK;
        break;

    case MMC_GET_CSD: /* Read CSD (16 uint8_ts) */
        if (send_cmd(CMD9, 0) == 0 && rcvr_datablock((uint8_t *)buff, 16))
        { /* READ_CSD */
            res = RES_OK;
        }
        break;

    case MMC_GET_CID: /* Read CID (16 uint8_ts) */
        if (send_cmd(CMD10, 0) == 0 && rcvr_datablock((uint8_t *)buff, 16))
        { /* READ_CID */
            res = RES_OK;
        }
        break;

    case MMC_GET_OCR: /* Read OCR (4 uint8_ts) */
        if (send_cmd(CMD58, 0) == 0)
        { /* READ_OCR */
            for (n = 0; n < 4; n++)
                *(((uint8_t *)buff) + n) = xchg_spi(0xFF);
            res = RES_OK;
        }
        break;

    case MMC_GET_SDSTAT: /* Read SD status (64 uint8_ts) */
        if (send_cmd(ACMD13, 0) == 0)
        { /* SD_STATUS */
            xchg_spi(0xFF);
            if (rcvr_datablock((uint8_t *)buff, 64))
                res = RES_OK;
        }
        break;
#if defined(CTRL_POWER_OFF)
    case CTRL_POWER_OFF: /* Power off */
        power_off();
        Stat |= STA_NOINIT;
        res = RES_OK;
        break;
#endif
#if _USE_ISDIO
    case ISDIO_READ:
        sdi = buff;
        if (send_cmd(CMD48, 0x80000000 | (DWORD)sdi->func << 28 | (DWORD)sdi->addr << 9 | ((sdi->ndata - 1) & 0x1FF)) == 0)
        {
            // for (Timer1 = 1000; (rc = xchg_spi(0xFF)) == 0xFF && Timer1; ) ;
            rc = xchg_spi(0xFF);
            while (cnt <= wt_ms && rc != 0xFF)
            {
                rc = xchg_spi(0xFF);
                board_delay_ms(1);
                cnt++;
            }

            if (rc == 0xFE)
            {
                for (bp = sdi->data, dc = sdi->ndata; dc; dc--)
                    *bp++ = xchg_spi(0xFF);

                for (dc = 514 - sdi->ndata; dc; dc--)
                    xchg_spi(0xFF);

                res = RES_OK;
            }
        }

        deselect();
        break;

    case ISDIO_WRITE:
        sdi = buff;
        if (send_cmd(CMD49, 0x80000000 | (DWORD)sdi->func << 28 | (DWORD)sdi->addr << 9 | ((sdi->ndata - 1) & 0x1FF)) == 0)
        {
            xchg_spi(0xFF);
            xchg_spi(0xFE);

            for (bp = sdi->data, dc = sdi->ndata; dc; dc--)
                xchg_spi(*bp++);

            for (dc = 514 - sdi->ndata; dc; dc--)
                xchg_spi(0xFF);

            if ((xchg_spi(0xFF) & 0x1F) == 0x05)
                res = RES_OK;
        }

        deselect();
        break;

    case ISDIO_MRITE:
        sdi = buff;
        if (send_cmd(CMD49, 0x84000000 | (DWORD)sdi->func << 28 | (DWORD)sdi->addr << 9 | sdi->ndata >> 8) == 0)
        {
            xchg_spi(0xFF);
            xchg_spi(0xFE);
            xchg_spi(sdi->ndata);

            for (dc = 513; dc; dc--)
                xchg_spi(0xFF);

            if ((xchg_spi(0xFF) & 0x1F) == 0x05)
                res = RES_OK;
        }

        deselect();
        break;
#endif
    default:
        res = RES_PARERR;
    }

    deselect();

    return res;
}

/*-----------------------------------------------------------------------*/
/* Device timer function                                                 */
/*-----------------------------------------------------------------------*/
/* This function must be called from timer interrupt routine in period
/  of 1 ms to generate card control timing.
*/
void mmc_disk_timerproc(void)
{
#if 0
    WORD n;
    uint8_t s;

    n = Timer1;                        /* 1kHz decrement timer stopped at 0 */
    if (n) Timer1 = --n;
    n = Timer2;
    if (n) Timer2 = --n;
    
    s = Stat;
    if (MMC_WP) {    /* Write protected */
        s |= STA_PROTECT;
    } else {        /* Write enabled */
        s &= ~STA_PROTECT;
    }
    if (MMC_CD) {    /* Card is in socket */
        s &= ~STA_NODISK;
    } else {        /* Socket empty */
        s |= (STA_NODISK | STA_NOINIT);
    }
    Stat = s;
#endif
}
