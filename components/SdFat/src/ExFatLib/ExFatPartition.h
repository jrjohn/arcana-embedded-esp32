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
#pragma once
/**
 * \file
 * \brief ExFatPartition include file.
 */
#include "../common/FsBlockDevice.h"
#include "../common/FsCache.h"
#include "../common/FsStructs.h"
#include "../common/SysCall.h"
/** Set EXFAT_READ_ONLY non-zero for read only */
#ifndef EXFAT_READ_ONLY
#define EXFAT_READ_ONLY 0
#endif  // EXFAT_READ_ONLY
/** Type for exFAT partition */
const uint8_t FAT_TYPE_EXFAT = 64;

class ExFatFile;
//------------------------------------------------------------------------------
/**
 * \struct DirPos_t
 * \brief Internal type for position in directory file.
 */
struct DirPos_t {
  /** current cluster */
  Cluster_t cluster;
  /** offset */
  uint32_t position;
  /** directory is contiguous */
  bool isContiguous;
};
//==============================================================================
/**
 * \class ExFatPartition
 * \brief Access exFat partitions on raw file devices.
 */
class ExFatPartition {
 public:
  ExFatPartition() = default;  // cppcheck-suppress uninitMemberVar
  /** \return the number of bytes in a cluster. */
  uint32_t bytesPerCluster() const { return m_bytesPerCluster; }
  /** \return the power of two for bytesPerCluster. */
  uint8_t bytesPerClusterShift() const {
    return m_bytesPerSectorShift + m_sectorsPerClusterShift;
  }
  /** \return the number of bytes in a sector. */
  uint16_t bytesPerSector() const { return m_bytesPerSector; }
  /** \return the power of two for bytesPerSector. */
  uint8_t bytesPerSectorShift() const { return m_bytesPerSectorShift; }

  /** Clear the cache and returns a pointer to the cache.  Not for normal apps.
   * \return A pointer to the cache buffer or zero if an error occurs.
   */
  uint8_t* cacheClear() { return m_dataCache.clear(); }
  /** \return the cluster count for the partition. */
  Cluster_t clusterCount() const { return m_clusterCount; }
#if USE_EXFAT_DUAL_FAT
  /** \return number of "free but referenced" clusters healed by the last
   * reconcileBitmap() at mount (0 on a clean volume; >0 after a torn commit). */
  uint32_t healedClusters() const { return m_healedClusters; }
#endif  // USE_EXFAT_DUAL_FAT
  /** \return the cluster heap start sector. */
  Cluster_t clusterHeapStartSector() const { return m_clusterHeapStartSector; }
  /** End access to volume
   * \return pointer to sector size buffer for format.
   */
  uint8_t* end() {
    m_fatType = 0;
    return cacheClear();
  }
  /** \return The number of File Allocation Tables. */
  uint8_t fatCount() const { return 1; }
  /** \return the FAT length in sectors */
  uint32_t fatLength() const { return m_fatLength; }
  /** \return the FAT start sector number. */
  Sector_t fatStartSector() const { return m_fatStartSector; }
  /** \return Type FAT_TYPE_EXFAT for exFAT partition or zero for error. */
  uint8_t fatType() const { return m_fatType; }
  /** \return number of FATs on the mounted volume (2 = dual-FAT, else 1). */
  uint8_t numberOfFats() const {
#if USE_EXFAT_DUAL_FAT
    return m_numberOfFats;
#else   // USE_EXFAT_DUAL_FAT
    return 1;
#endif  // USE_EXFAT_DUAL_FAT
  }
  /** \return free cluster count or -1 if an error occurs. */
  Cluster_t freeClusterCount();
  /** Initialize a exFAT partition.
   * \param[in] dev The blockDevice for the partition.
   * \param[in] part The partition to be used.  Legal values for \a part are
   * 1-4 to use the corresponding partition on a device formatted with
   * a MBR, Master Boot Record, or zero if the device is formatted as
   * a super floppy with the FAT boot sector in sector startSector.
   * \param[in] startSector location of volume if part is zero.
   *
   * \return true for success or false for failure.
   */
  bool init(FsBlockDevice* dev, uint8_t part, Sector_t startSector = 0);
  /**
   * Check for device busy.
   *
   * \return true if busy else false.
   */
  bool isBusy() { return m_blockDev->isBusy(); }
  /** \return the root directory start cluster number. */
  Cluster_t rootDirectoryCluster() const { return m_rootDirectoryCluster; }
  /** \return the root directory length. */
  uint32_t rootLength();
  /** \return the number of sectors in a cluster. */
  Sector_t sectorsPerCluster() const { return 1UL << m_sectorsPerClusterShift; }
  /** \return the power of two for sectors per cluster. */
  uint8_t sectorsPerClusterShift() const { return m_sectorsPerClusterShift; }
  //----------------------------------------------------------------------------
#ifndef DOXYGEN_SHOULD_SKIP_THIS
  void checkUpcase(print_t* pr);
  bool printDir(print_t* pr, ExFatFile* file);
  void dmpBitmap(print_t* pr);
  void dmpCluster(print_t* pr, Cluster_t cluster, uint32_t offset,
                  uint32_t count);
  void dmpFat(print_t* pr, uint32_t start, uint32_t count);
  void dmpSector(print_t* pr, Sector_t sector, uint8_t w = 16);
  bool printVolInfo(print_t* pr);
  void printFat(print_t* pr);
  void printUpcase(print_t* pr);
#endif  // DOXYGEN_SHOULD_SKIP_THIS
  //----------------------------------------------------------------------------
 private:
  /** ExFatFile allowed access to private members. */
  friend class ExFatFile;
  /** ExFatVolume (mount-time bitmap reconcile) allowed access to private members. */
  friend class ExFatVolume;
  uint32_t bitmapFind(Cluster_t cluster, uint32_t count);
  bool bitmapModify(Cluster_t cluster, uint32_t count, bool value);
#if USE_EXFAT_DUAL_FAT
  // Idempotently mark [cluster, cluster+count) used in the working bitmap (set
  // bits, never error on an already-set bit). Used by ExFatVolume::reconcileBitmap
  // to heal "free but referenced" clusters after a torn dual-FAT commit.
  bool bitmapMarkUsed(Cluster_t cluster, uint32_t count);
#endif  // USE_EXFAT_DUAL_FAT
  //----------------------------------------------------------------------------
  // Cache functions.
  uint8_t* bitmapCachePrepare(Sector_t sector, uint8_t option) {
#if USE_EXFAT_BITMAP_CACHE
    return m_bitmapCache.prepare(sector, option);
#else   // USE_EXFAT_BITMAP_CACHE
    return m_dataCache.prepare(sector, option);
#endif  // USE_EXFAT_BITMAP_CACHE
  }
  void cacheInit(FsBlockDevice* dev) {
#if USE_EXFAT_BITMAP_CACHE
    m_bitmapCache.init(dev);
#endif  // USE_EXFAT_BITMAP_CACHE
    m_dataCache.init(dev);
  }
  bool cacheSync() {
#if USE_EXFAT_BITMAP_CACHE
    return m_bitmapCache.sync() && m_dataCache.sync() && syncDevice();
#else   // USE_EXFAT_BITMAP_CACHE
    return m_dataCache.sync() && syncDevice();
#endif  // USE_EXFAT_BITMAP_CACHE
  }
  void dataCacheDirty() { m_dataCache.dirty(); }
  void dataCacheInvalidate() { m_dataCache.invalidate(); }
  uint8_t* dataCachePrepare(Sector_t sector, uint8_t option) {
    return m_dataCache.prepare(sector, option);
  }
  Sector_t dataCacheSector() { return m_dataCache.sector(); }
  bool dataCacheSync() { return m_dataCache.sync(); }
  //----------------------------------------------------------------------------
  uint32_t clusterMask() const { return m_clusterMask; }
  Sector_t clusterStartSector(Cluster_t cluster) {
    return m_clusterHeapStartSector +
           ((cluster - 2) << m_sectorsPerClusterShift);
  }
  uint8_t* dirCache(const DirPos_t* pos, uint8_t options);
  int8_t dirSeek(DirPos_t* pos, uint32_t offset);
  int8_t fatGet(Cluster_t cluster, Cluster_t* value);
  bool fatPut(Cluster_t cluster, Cluster_t value);
  // Atomically commit metadata: flush the working FAT/bitmap, flip the ActiveFat
  // bit (single boot-sector write), then resync the new working copy. On a stock
  // (single-FAT) volume — or a non-dual-FAT build — this is just cacheSync().
  bool commit();
  Cluster_t chainSize(Cluster_t cluster);
  bool freeChain(Cluster_t cluster);
  uint16_t sectorMask() const { return m_sectorMask; }
  bool syncDevice() { return m_blockDev->syncDevice(); }
  bool cacheSafeRead(Sector_t sector, uint8_t* dst) {
    return m_dataCache.cacheSafeRead(sector, dst);
  }
  bool cacheSafeWrite(Sector_t sector, const uint8_t* src) {
    return m_dataCache.cacheSafeWrite(sector, src);
  }
  bool cacheSafeRead(Sector_t sector, uint8_t* dst, size_t count) {
    return m_dataCache.cacheSafeRead(sector, dst, count);
  }
  bool cacheSafeWrite(Sector_t sector, const uint8_t* src, size_t count) {
    return m_dataCache.cacheSafeWrite(sector, src, count);
  }
  bool readSector(Sector_t sector, uint8_t* dst) {
    return m_blockDev->readSector(sector, dst);
  }
  bool writeSector(Sector_t sector, const uint8_t* src) {
    return m_blockDev->writeSector(sector, src);
  }
  //----------------------------------------------------------------------------
  static const uint8_t m_bytesPerSectorShift = 9;
  static const uint16_t m_bytesPerSector = 1 << m_bytesPerSectorShift;
  static const uint16_t m_sectorMask = m_bytesPerSector - 1;
  //----------------------------------------------------------------------------
#if USE_EXFAT_BITMAP_CACHE
  FsCache m_bitmapCache;
#endif  // USE_EXFAT_BITMAP_CACHE
  FsCache m_dataCache;
  Sector_t m_bitmapStart;
  Sector_t m_fatStartSector;
  Sector_t m_bitmapStartSector;     // active allocation bitmap (dual-FAT: per ActiveFat)
  uint32_t m_fatLength;
  Sector_t m_clusterHeapStartSector;
  Cluster_t m_clusterCount;
  Cluster_t m_rootDirectoryCluster;
  uint32_t m_clusterMask;
  uint32_t m_bytesPerCluster;
  FsBlockDevice* m_blockDev;
  uint8_t m_fatType = 0;
  uint8_t m_sectorsPerClusterShift;
#if USE_EXFAT_DUAL_FAT
  uint8_t  m_numberOfFats = 1;
  uint8_t  m_activeFat = 0;         // committed FAT/bitmap copy (ActiveFat bit)
  Sector_t m_volBootSector = 0;     // partition boot sector — target of the atomic flip
  Sector_t m_fatBaseSector = 0;     // FAT #0 start (FAT #1 = base + fatLength)
  // Transaction dirty tracking — FAT/bitmap sector offsets (within each region)
  // written since the last commit, used to resync the working copy after the flip.
  // On overflow we fall back to a full reseed (correct, just slower).
  static const uint16_t kMaxTxSectors = 256;
  uint32_t m_txFat[kMaxTxSectors];
  uint32_t m_txBmp[kMaxTxSectors];
  uint16_t m_txFatCount = 0;
  uint16_t m_txBmpCount = 0;
  bool     m_txOverflow = false;
  uint32_t m_healedClusters = 0;    // count of free-but-referenced clusters healed at last mount
  void txRecord(uint32_t* list, uint16_t* count, uint32_t off);
  bool reseedInactive();            // copy active FAT/bitmap -> inactive (up to high-water)
  uint32_t bitmapHighWater(Sector_t bmpStart, uint32_t bmpSectors);
#endif  // USE_EXFAT_DUAL_FAT
};
