#include <loadcore.h>
#include <stdio.h>
#include <sysclib.h>
#include <atad.h>
#include <iomanX.h>
#include <errno.h>
#include <thbase.h>
#include <sifman.h>
#include <intrman.h>

#include "opl-hdd-ioctl.h"

#define MODNAME "xhdd"
IRX_ID(MODNAME, 1, 2);

static int isHDPro;

static u64 lba_pos = 0;
static u64 io_timer = 0;

extern int ata_device_set_transfer_mode(int device, int type, int mode);

static u32 ComputeTimeDiff(iop_sys_clock_t* pStart,iop_sys_clock_t* pEnd)
{
	iop_sys_clock_t	Diff;
	u32 iSec, iUSec, iDiff;

	Diff.lo=pEnd->lo-pStart->lo;
	Diff.hi=pEnd->hi-pStart->hi - (pStart->lo>pEnd->lo);

	SysClock2USec(&Diff, &iSec, &iUSec);
	iDiff=(iSec*1000)+(iUSec/1000);

	return ((iDiff != 0) ? iDiff : 1);
}

static void BlockRead(hddAtaReadTest_t* pArgs)
{
    SifDmaTransfer_t dmaInfo;
    int dmaId;
    int intrState;

    // TODO: Add time calculation once I figure out why GetSystemTime doesn't work...

    // Read the data from the HDD.
    ata_device_sector_io64(0, pArgs->iop_buffer, lba_pos, pArgs->block_size_in_sectors, ATA_DIR_READ);
    lba_pos += pArgs->block_size_in_sectors;

    // Check if we should copy it to the EE.
    if (pArgs->copy_to_ee != 0)
    {
        // Setup and kickoff the dma transfer.
        dmaInfo.src = pArgs->iop_buffer;
        dmaInfo.dest = pArgs->ee_buffer;
        dmaInfo.size = pArgs->block_size_in_bytes;
        dmaInfo.attr = 0;

        CpuSuspendIntr(&intrState);
        dmaId = sceSifSetDma(&dmaInfo, 1);
        CpuResumeIntr(intrState);

        // Wait for the dma operation to complete.
        while (sceSifDmaStat(dmaId) >= 0);
    }
}

static int xhddInit(iop_device_t *device)
{
    // Force atad to initialize the hdd devices.
    ata_get_devinfo(0);

    return 0;
}

static int xhddOpen(iomanX_iop_file_t *f, const char *name, int flags, int mode)
{
    if (f->unit >= 2)
        return -ENODEV;

    // Reset the file position.
    lba_pos = 0;

    return 0;
}

static int xhddClose(iomanX_iop_file_t *f)
{
    return 0;
}

static int xhddRead(iomanX_iop_file_t *f, void *buf, int size)
{
    //iop_sys_clock_t startTime;
    //iop_sys_clock_t endTime;

    if (f->unit >= 2)
        return -ENXIO;

    // Make sure the read size is a multiple of the sector size.
    if (size % 512 != 0)
        return -EINVAL;

    u32 sectorCount = size / 512;

    // Read the sector(s) from the drive.
    //GetSystemTime(&startTime);
    int ret = ata_device_sector_io64(f->unit, buf, lba_pos, sectorCount, ATA_DIR_READ);
    //GetSystemTime(&endTime);
    
    //io_timer += ComputeTimeDiff(&startTime, &endTime);

    lba_pos += sectorCount;

    return ret;
}

static s64 xhddLseek64(iomanX_iop_file_t *f, s64 pos, int whence)
{
    switch (whence)
    {
        case FIO_SEEK_SET: lba_pos = (u64)pos; break;
        case FIO_SEEK_CUR: lba_pos += (u64)pos; break;
        case FIO_SEEK_END: return -EINVAL;
    }

    return 0;
}

static int xhddUnsupported(void)
{
    return -1;
}

static int xhddDevctl(iop_file_t *fd, const char *name, int cmd, void *arg, unsigned int arglen, void *buf, unsigned int buflen)
{
    if (fd->unit >= 2)
        return -ENXIO;

    switch (cmd) {
        case ATA_DEVCTL_SET_TRANSFER_MODE:
        {
            if (!isHDPro)
                return ata_device_set_transfer_mode(fd->unit, ((hddAtaSetMode_t *)arg)->type, ((hddAtaSetMode_t *)arg)->mode);
            else
                return -ENXIO;
        }
        case ATA_DEVCTL_IDENTIFY:
        {
            // Make sure the output buffer is large enough to hold the ATA_IDENTIFY structure.
            if (buflen < 512)
                return -EINVAL;

            return ata_device_identify(fd->unit, buf);
        }
        case ATA_DEVCTL_GET_CRC_ERROR_COUNT:
        {
            // Make sure the output buffer is large enough.
            if (buflen < sizeof(u64))
                return -EINVAL;

            *(u64*)buf = ata_get_crc_error_count();
            return 0;
        }
        case ATA_DEVCTL_RESET_CRC_ERROR_COUNT:
        {
            ata_reset_crc_error_count();
            return 0;
        }
        case ATA_DEVCTL_START_TIMER:
        {
            io_timer = 0;
            return 0;
        }
        case ATA_DEVCTL_GET_TIMER:
        {
            // Make sure the output buffer is large enough.
            if (buflen < sizeof(u64))
                return -EINVAL;

            *(u64*)buf = io_timer;
            return 0;
        }
        case ATA_DEVCTL_READ_BLOCK:
        {
            // Make sure the input buffer is valid.
            if (arg == NULL || arglen < sizeof(hddAtaReadTest_t))
                return -EINVAL;

            BlockRead((hddAtaReadTest_t*)arg);
            return 0;
        }
        case ATA_DEVCTL_FLUSH_CACHE:
        {
            return ata_device_flush_cache(fd->unit);
        }
        default:
            return -EINVAL;
    }
}

static iop_device_ops_t xhdd_ops = {
    &xhddInit,
    (void *)&xhddUnsupported,
    (void *)&xhddUnsupported,
    &xhddOpen,
    &xhddClose,
    &xhddRead,
    (void *)&xhddUnsupported,
    (void *)&xhddUnsupported,
    (void *)&xhddUnsupported,
    (void *)&xhddUnsupported,
    (void *)&xhddUnsupported,
    (void *)&xhddUnsupported,
    (void *)&xhddUnsupported,
    (void *)&xhddUnsupported,
    (void *)&xhddUnsupported,
    (void *)&xhddUnsupported,
    (void *)&xhddUnsupported,
    (void *)&xhddUnsupported,
    (void *)&xhddUnsupported,
    (void *)&xhddUnsupported,
    (void *)&xhddUnsupported,
    (void *)&xhddUnsupported,
    &xhddLseek64,
    &xhddDevctl,
};

static iop_device_t xhddDevice = {
    "xhdd",
    IOP_DT_BLOCK | IOP_DT_FSEXT,
    1,
    "XHDD",
    &xhdd_ops};

int _start(int argc, char *argv[])
{
    int i;

    isHDPro = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-hdpro"))
            isHDPro = 1;
    }

    return AddDrv(&xhddDevice) == 0 ? MODULE_RESIDENT_END : MODULE_NO_RESIDENT_END;
}
