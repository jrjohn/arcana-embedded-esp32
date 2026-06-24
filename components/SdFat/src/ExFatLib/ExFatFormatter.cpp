/**
 * Copyright (c) 2011-2025 Bill Greiman
 * This file is part of the SdFat library for SD memory cards.
 *
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#define DBG_FILE "ExFatFormatter.cpp"
#include "../common/DebugMacros.h"
#include "../common/upcase.h"
#include "ExFatLib.h"
//------------------------------------------------------------------------------
// Formatter assumes 512 byte sectors.
const uint32_t BOOT_BACKUP_OFFSET = 12;
const uint16_t BYTES_PER_SECTOR = 512;
const uint16_t SECTOR_MASK = BYTES_PER_SECTOR - 1;
const uint8_t BYTES_PER_SECTOR_SHIFT = 9;
const uint16_t MINIMUM_UPCASE_SKIP = 512;
// Number of FATs / allocation bitmaps. dual-FAT uses 2 of each for transaction
// safety; stock exFAT uses 1. Heap layout: bitmap[0..nBitmaps-1] at clusters
// 2.., then up-case, then root — so single-FAT keeps UPCASE=3 / ROOT=4.
#if USE_EXFAT_DUAL_FAT
const uint8_t   kNumFats = 2;
const uint8_t   kNumBitmaps = 2;
#else
const uint8_t   kNumFats = 1;
const uint8_t   kNumBitmaps = 1;
#endif
const uint8_t   kMetaClusters = kNumBitmaps + 2;        // bitmaps + upcase + root
const Cluster_t BITMAP_CLUSTER = 2;
const Cluster_t UPCASE_CLUSTER = 2 + kNumBitmaps;
const Cluster_t ROOT_CLUSTER = 3 + kNumBitmaps;
//------------------------------------------------------------------------------
#define PRINT_FORMAT_PROGRESS 1
#if !PRINT_FORMAT_PROGRESS
#define writeMsg(pr, str)
#elif defined(__AVR__)
#define writeMsg(pr, str) \
  if (pr) pr->print(F(str))
#else  // PRINT_FORMAT_PROGRESS
#define writeMsg(pr, str) \
  if (pr) pr->write(str)
#endif  // PRINT_FORMAT_PROGRESS
//------------------------------------------------------------------------------
bool ExFatFormatter::format(FsBlockDevice* dev, uint8_t* secBuf, print_t* pr) {
#if !PRINT_FORMAT_PROGRESS
  (void)pr;
#endif  //  !PRINT_FORMAT_PROGRESS
  MbrSector_t* mbr;
  ExFatPbs_t* pbs;
  DirUpcase_t* dup;
  DirBitmap_t* dbm;
  DirLabel_t* label;
  uint32_t bitmapSize;
  uint32_t checksum = 0;
  Cluster_t clusterCount;
  Cluster_t clusterHeapOffset;
  uint32_t fatLength;
  uint32_t fatOffset;
  uint32_t m;
  uint32_t ns;
  uint32_t partitionOffset;
  Sector_t sector;
  Sector_t sectorsPerCluster;
  uint32_t volumeLength;
  Sector_t sectorCount;
  uint8_t sectorsPerClusterShift;
  uint8_t vs;

  m_dev = dev;
  m_secBuf = secBuf;
  sectorCount = dev->sectorCount();
  // Min size is 512 MB
  if (sectorCount < 0X100000) {
    writeMsg(pr, "Device is too small\r\n");
    DBG_FAIL_MACRO;
    goto fail;
  }
  // Determine partition layout.
  for (m = 1, vs = 0; m && sectorCount > m; m <<= 1, vs++) {
  }
  sectorsPerClusterShift = vs < 29 ? 8 : (vs - 11) / 2;
  sectorsPerCluster = 1UL << sectorsPerClusterShift;
  fatLength = 1UL << (vs < 27 ? 13 : (vs + 1) / 2);
  fatOffset = fatLength;
  partitionOffset = 2 * fatLength;
  // dual-FAT: two FATs + two allocation bitmaps. The cluster heap moves out past
  // the second FAT, and the first (nBitmaps + 2) heap clusters hold the bitmaps,
  // up-case table, and root directory.
  clusterHeapOffset = (kNumFats + 1) * fatLength;
  clusterCount = (sectorCount - partitionOffset - clusterHeapOffset)
                 >> sectorsPerClusterShift;
  volumeLength = clusterHeapOffset + (clusterCount << sectorsPerClusterShift);

  // make Master Boot Record.  Use fake CHS.
  memset(secBuf, 0, BYTES_PER_SECTOR);
  mbr = reinterpret_cast<MbrSector_t*>(secBuf);
  mbr->part->beginCHS[0] = 1;
  mbr->part->beginCHS[1] = 1;
  mbr->part->beginCHS[2] = 0;
  mbr->part->type = 7;
  mbr->part->endCHS[0] = 0XFE;
  mbr->part->endCHS[1] = 0XFF;
  mbr->part->endCHS[2] = 0XFF;
  setLe32(mbr->part->startSector, partitionOffset);
  setLe32(mbr->part->totalSectors, volumeLength);
  setLe16(mbr->signature, MBR_SIGNATURE);
  if (!dev->writeSector(0, secBuf)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // Partition Boot sector.
  memset(secBuf, 0, BYTES_PER_SECTOR);
  pbs = reinterpret_cast<ExFatPbs_t*>(secBuf);
  pbs->jmpInstruction[0] = 0XEB;
  pbs->jmpInstruction[1] = 0X76;
  pbs->jmpInstruction[2] = 0X90;
  pbs->oemName[0] = 'E';
  pbs->oemName[1] = 'X';
  pbs->oemName[2] = 'F';
  pbs->oemName[3] = 'A';
  pbs->oemName[4] = 'T';
  pbs->oemName[5] = ' ';
  pbs->oemName[6] = ' ';
  pbs->oemName[7] = ' ';
  setLe64(pbs->bpb.partitionOffset, partitionOffset);
  setLe64(pbs->bpb.volumeLength, volumeLength);
  setLe32(pbs->bpb.fatOffset, fatOffset);
  setLe32(pbs->bpb.fatLength, fatLength);
  setLe32(pbs->bpb.clusterHeapOffset, clusterHeapOffset);
  setLe32(pbs->bpb.clusterCount, clusterCount);
  setLe32(pbs->bpb.rootDirectoryCluster, ROOT_CLUSTER);
  setLe32(pbs->bpb.volumeSerialNumber, sectorCount);
  setLe16(pbs->bpb.fileSystemRevision, 0X100);
  setLe16(pbs->bpb.volumeFlags, 0);
  pbs->bpb.bytesPerSectorShift = BYTES_PER_SECTOR_SHIFT;
  pbs->bpb.sectorsPerClusterShift = sectorsPerClusterShift;
  pbs->bpb.numberOfFats = kNumFats;
  pbs->bpb.driveSelect = 0X80;
  pbs->bpb.percentInUse = 0;

  // Fill boot code like official SDFormatter.
  for (size_t i = 0; i < sizeof(pbs->bootCode); i++) {
    pbs->bootCode[i] = 0XF4;
  }
  setLe16(pbs->signature, PBR_SIGNATURE);
  for (size_t i = 0; i < BYTES_PER_SECTOR; i++) {
    if (i == offsetof(ExFatPbs_t, bpb.volumeFlags[0]) ||
        i == offsetof(ExFatPbs_t, bpb.volumeFlags[1]) ||
        i == offsetof(ExFatPbs_t, bpb.percentInUse)) {
      continue;
    }
    checksum = exFatChecksum(checksum, secBuf[i]);
  }
  sector = partitionOffset;
  if (!dev->writeSector(sector, secBuf) ||
      !dev->writeSector(sector + BOOT_BACKUP_OFFSET, secBuf)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  sector++;
  // Write eight Extended Boot Sectors.
  memset(secBuf, 0, BYTES_PER_SECTOR);
  setLe16(pbs->signature, PBR_SIGNATURE);
  for (int j = 0; j < 8; j++) {
    for (size_t i = 0; i < BYTES_PER_SECTOR; i++) {
      checksum = exFatChecksum(checksum, secBuf[i]);
    }
    if (!dev->writeSector(sector, secBuf) ||
        !dev->writeSector(sector + BOOT_BACKUP_OFFSET, secBuf)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    sector++;
  }
  // Write OEM Parameter Sector and reserved sector.
  memset(secBuf, 0, BYTES_PER_SECTOR);
  for (int j = 0; j < 2; j++) {
    for (size_t i = 0; i < BYTES_PER_SECTOR; i++) {
      checksum = exFatChecksum(checksum, secBuf[i]);
    }
    if (!dev->writeSector(sector, secBuf) ||
        !dev->writeSector(sector + BOOT_BACKUP_OFFSET, secBuf)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    sector++;
  }
  // Write Boot CheckSum Sector.
  for (size_t i = 0; i < BYTES_PER_SECTOR; i += 4) {
    setLe32(secBuf + i, checksum);
  }
  if (!dev->writeSector(sector, secBuf) ||
      !dev->writeSector(sector + BOOT_BACKUP_OFFSET, secBuf)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // Initialize FAT(s). dual-FAT writes both FATs identically at format time.
  writeMsg(pr, "Writing FAT ");
  ns = ((clusterCount + 2) * 4 + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR;
  for (uint8_t f = 0; f < kNumFats; f++) {
    sector = partitionOffset + fatOffset + (uint32_t)f * fatLength;
    memset(secBuf, 0, BYTES_PER_SECTOR);
    // Reserved clusters 0,1 + the (nBitmaps + 2) metadata clusters are EOC.
    secBuf[0] = 0XF8;
    for (size_t i = 1; i < 4u * (2 + kMetaClusters); i++) {
      secBuf[i] = 0XFF;
    }
    for (uint32_t i = 0; i < ns; i++) {
      if (i % (ns / 32) == 0) {
        writeMsg(pr, ".");
      }
      if (!dev->writeSector(sector + i, secBuf)) {
        DBG_FAIL_MACRO;
        goto fail;
      }
      if (i == 0) {
        memset(secBuf, 0, BYTES_PER_SECTOR);
      }
    }
  }
  writeMsg(pr, "\r\n");
  // Write the allocation bitmap(s) — dual-FAT has two, each its own cluster.
  bitmapSize = (clusterCount + 7) / 8;
  ns = (bitmapSize + BYTES_PER_SECTOR - 1) / BYTES_PER_SECTOR;
  if (ns > sectorsPerCluster) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  for (uint8_t b = 0; b < kNumBitmaps; b++) {
    sector = partitionOffset + clusterHeapOffset + (uint32_t)b * sectorsPerCluster;
    memset(secBuf, 0, BYTES_PER_SECTOR);
    // Mark the (nBitmaps + 2) metadata clusters (bitmaps, upcase, root) allocated.
    secBuf[0] = (1u << kMetaClusters) - 1;
    for (uint32_t i = 0; i < ns; i++) {
      if (!dev->writeSector(sector + i, secBuf)) {
        DBG_FAIL_MACRO;
        goto fail;
      }
      if (i == 0) {
        secBuf[0] = 0;
      }
    }
  }
  // Write the up-case table (cluster after the bitmaps).
  writeMsg(pr, "Writing upcase table\r\n");
  if (!writeUpcase(partitionOffset + clusterHeapOffset +
                   (uint32_t)kNumBitmaps * sectorsPerCluster)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  if (m_upcaseSize > BYTES_PER_SECTOR * sectorsPerCluster) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  // Initialize first sector of root (the cluster after upcase).
  writeMsg(pr, "Writing root\r\n");
  ns = sectorsPerCluster;
  sector = partitionOffset + clusterHeapOffset +
           (uint32_t)(ROOT_CLUSTER - 2) * sectorsPerCluster;
  memset(secBuf, 0, BYTES_PER_SECTOR);

  // Volume Label "ARCANA" (UTF-16LE) so the card shows a friendly name in
  // macOS/Windows file managers instead of "Untitled".
  label = reinterpret_cast<DirLabel_t*>(secBuf);
  label->type = EXFAT_TYPE_LABEL;            // in-use (bit 7 set)
  static const char kVolumeLabel[] = "ARCANA";
  label->labelLength = sizeof(kVolumeLabel) - 1;
  for (size_t i = 0; i < sizeof(kVolumeLabel) - 1; i++) {
    label->unicode[2 * i] = (uint8_t)kVolumeLabel[i];
    label->unicode[2 * i + 1] = 0;
  }

  // Allocation Bitmap directory entries (one per bitmap; flags bit0 = bitmap id).
  for (uint8_t b = 0; b < kNumBitmaps; b++) {
    dbm = reinterpret_cast<DirBitmap_t*>(secBuf + 32 * (1 + b));
    dbm->type = EXFAT_TYPE_BITMAP;
    dbm->flags = b;  // BitmapIdentifier: 0 = first, 1 = second (dual-FAT)
    setLe32(dbm->firstCluster, BITMAP_CLUSTER + b);
    setLe64(dbm->size, bitmapSize);
  }

  // upcase directory entry (after the bitmap entries).
  dup = reinterpret_cast<DirUpcase_t*>(secBuf + 32 * (1 + kNumBitmaps));
  dup->type = EXFAT_TYPE_UPCASE;
  setLe32(dup->checksum, m_upcaseChecksum);
  setLe32(dup->firstCluster, UPCASE_CLUSTER);
  setLe64(dup->size, m_upcaseSize);

  // Write root, cluster four.
  for (uint32_t i = 0; i < ns; i++) {
    if (!dev->writeSector(sector + i, secBuf)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    if (i == 0) {
      memset(secBuf, 0, BYTES_PER_SECTOR);
    }
  }
  writeMsg(pr, "Format done\r\n");
  return true;

fail:
  writeMsg(pr, "Format failed\r\n");
  return false;
}
//------------------------------------------------------------------------------
bool ExFatFormatter::syncUpcase() {
  uint16_t index = m_upcaseSize & SECTOR_MASK;
  if (!index) {
    return true;
  }
  for (size_t i = index; i < BYTES_PER_SECTOR; i++) {
    m_secBuf[i] = 0;
  }
  return m_dev->writeSector(m_upcaseSector, m_secBuf);
}
//------------------------------------------------------------------------------
bool ExFatFormatter::writeUpcaseByte(uint8_t b) {
  uint16_t index = m_upcaseSize & SECTOR_MASK;
  m_secBuf[index] = b;
  m_upcaseChecksum = exFatChecksum(m_upcaseChecksum, b);
  m_upcaseSize++;
  if (index == SECTOR_MASK) {
    return m_dev->writeSector(m_upcaseSector++, m_secBuf);
  }
  return true;
}
//------------------------------------------------------------------------------
bool ExFatFormatter::writeUpcaseUnicode(uint16_t unicode) {
  return writeUpcaseByte(unicode) && writeUpcaseByte(unicode >> 8);
}
//------------------------------------------------------------------------------
bool ExFatFormatter::writeUpcase(Sector_t sector) {
  uint32_t n;
  uint32_t ns;
  uint32_t ch = 0;
  uint16_t uc;

  m_upcaseSize = 0;
  m_upcaseChecksum = 0;
  m_upcaseSector = sector;

  while (ch < 0X10000) {
    uc = toUpcase(ch);
    if (uc != ch) {
      if (!writeUpcaseUnicode(uc)) {
        DBG_FAIL_MACRO;
        goto fail;
      }
      ch++;
    } else {
      for (n = ch + 1; n < 0X10000 && n == toUpcase(n); n++) {
      }
      ns = n - ch;
      if (ns >= MINIMUM_UPCASE_SKIP) {
        if (!writeUpcaseUnicode(0XFFFF) || !writeUpcaseUnicode(ns)) {
          DBG_FAIL_MACRO;
          goto fail;
        }
        ch = n;
      } else {
        while (ch < n) {
          if (!writeUpcaseUnicode(ch++)) {
            DBG_FAIL_MACRO;
            goto fail;
          }
        }
      }
    }
  }
  if (!syncUpcase()) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  return true;

fail:
  return false;
}
