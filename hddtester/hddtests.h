
#ifndef _HDDTESTS_H
#define _HDDTESTS_H

#include <tamtypes.h>

#define HDD_SECTOR_SIZE     512
#define ONE_MB_IN_BYTES     1024 * 1024

#define HDD_ERR_FAILED_TO_OPEN      1
#define HDD_ERR_OUT_OF_MEMORY       2

int SequentialRawReadTest(int mbToRead, int blockSizeKb, void* pIopBuffer, int fullPass, u64* pCrcErrorCount, u64* pElapsedTimeEE, u64* pElapsedTimeIOP);
int RandomRawReadTest(int mbToRead, int blockSizeKb, void* pIopBuffer, int fullPass, u64 hddMaxLBA, u64* pCrcErrorCount, u64* pElapsedTimeEE, u64* pElapsedTimeIOP);

#endif