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
#define DBG_FILE "ExFatPartition.cpp"
#include "../common/DebugMacros.h"
#include "ExFatLib.h"
//------------------------------------------------------------------------------
// return 0 if error, 1 if no space, else start cluster.
Cluster_t ExFatPartition::bitmapFind(Cluster_t cluster, uint32_t count) {
  Cluster_t start = cluster ? cluster - 2 : m_bitmapStart;
  if (start >= m_clusterCount) {
    start = 0;
  }
  Cluster_t endAlloc = start;
  Cluster_t bgnAlloc = start;
  uint16_t sectorSize = 1 << m_bytesPerSectorShift;
  size_t i = (start >> 3) & (sectorSize - 1);
  const uint8_t* cache;
  uint8_t mask = 1 << (start & 7);
  while (true) {
    Sector_t sector =
        m_bitmapStartSector + (endAlloc >> (m_bytesPerSectorShift + 3));
    cache = bitmapCachePrepare(sector, FsCache::CACHE_FOR_READ);
    if (!cache) {
      return 0;
    }
    for (; i < sectorSize; i++) {
      for (; mask; mask <<= 1) {
        endAlloc++;
        if (!(mask & cache[i])) {
          if ((endAlloc - bgnAlloc) == count) {
            if (cluster == 0 && count == 1) {
              // Start at found sector.  bitmapModify may increase this.
              m_bitmapStart = bgnAlloc;
            }
            return bgnAlloc + 2;
          }
        } else {
          bgnAlloc = endAlloc;
        }
        if (endAlloc == start) {
          return 1;
        }
        if (endAlloc >= m_clusterCount) {
          endAlloc = bgnAlloc = 0;
          i = sectorSize;
          break;
        }
      }
      mask = 1;
    }
    i = 0;
  }
  return 0;
}
//------------------------------------------------------------------------------
bool ExFatPartition::bitmapModify(Cluster_t cluster, uint32_t count,
                                  bool value) {
  Sector_t sector;
  Cluster_t start = cluster - 2;
  size_t i;
  uint8_t* cache;
  uint8_t mask;
  cluster -= 2;
  if ((start + count) > m_clusterCount) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  if (value) {
    if (start <= m_bitmapStart && m_bitmapStart < (start + count)) {
      m_bitmapStart = (start + count) < m_clusterCount ? start + count : 0;
    }
  } else {
    if (start < m_bitmapStart) {
      m_bitmapStart = start;
    }
  }
  mask = 1 << (start & 7);
  sector = m_bitmapStartSector + (start >> (m_bytesPerSectorShift + 3));
  i = (start >> 3) & m_sectorMask;
  while (true) {
#if USE_EXFAT_DUAL_FAT
    if (m_numberOfFats == 2) {
      txRecord(m_txBmp, &m_txBmpCount, sector - m_bitmapStartSector);
    }
#endif  // USE_EXFAT_DUAL_FAT
    cache = bitmapCachePrepare(sector++, FsCache::CACHE_FOR_WRITE);
    if (!cache) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    for (; i < m_bytesPerSector; i++) {
      for (; mask; mask <<= 1) {
        if (value == static_cast<bool>(cache[i] & mask)) {
          DBG_FAIL_MACRO;
          goto fail;
        }
        cache[i] ^= mask;
        if (--count == 0) {
          return true;
        }
      }
      mask = 1;
    }
    i = 0;
  }

fail:
  return false;
}
#if USE_EXFAT_DUAL_FAT
//------------------------------------------------------------------------------
// Idempotently mark [cluster, cluster+count) used in the WORKING bitmap. Mirrors
// bitmapModify's sector/bit walk but ORs the bits (setting an already-set bit is
// a no-op, not an error). Tx-records the touched sectors so the heal is carried
// into the committed copy on the next commit. See ExFatVolume::reconcileFile.
bool ExFatPartition::bitmapMarkUsed(Cluster_t cluster, uint32_t count) {
  if (cluster < 2 || count == 0) {
    return true;  // LCOV_EXCL_LINE — defensive: callers (reconcileFile) pass cluster>=2, count>=1
  }
  Cluster_t start = cluster - 2;
  if ((start + count) > m_clusterCount) {
    DBG_FAIL_MACRO;        // LCOV_EXCL_LINE — defensive: referenced clusters are in range
    return false;          // LCOV_EXCL_LINE
  }
  uint8_t mask = 1 << (start & 7);
  Sector_t sector = m_bitmapStartSector + (start >> (m_bytesPerSectorShift + 3));
  size_t i = (start >> 3) & m_sectorMask;
  while (true) {
    if (m_numberOfFats == 2) {
      txRecord(m_txBmp, &m_txBmpCount, sector - m_bitmapStartSector);
    }
    uint8_t* cache = bitmapCachePrepare(sector++, FsCache::CACHE_FOR_WRITE);
    if (!cache) {
      DBG_FAIL_MACRO;        // LCOV_EXCL_LINE — defensive: bitmap sector read can't fail here
      return false;          // LCOV_EXCL_LINE
    }
    for (; i < m_bytesPerSector; i++) {
      for (; mask; mask <<= 1) {
        if (!(cache[i] & mask)) {  // was free but referenced — heal it
          cache[i] |= mask;
          m_healedClusters++;
        }
        if (--count == 0) {
          return true;
        }
      }
      mask = 1;
    }
    i = 0;
  }
}
#endif  // USE_EXFAT_DUAL_FAT
//------------------------------------------------------------------------------
uint32_t ExFatPartition::chainSize(Cluster_t cluster) {
  uint32_t n = 0;
  int8_t status;
  do {
    status = fatGet(cluster, &cluster);
    if (status < 0) return 0;
    n++;
  } while (status);
  return n;
}
//------------------------------------------------------------------------------
uint8_t* ExFatPartition::dirCache(const DirPos_t* pos, uint8_t options) {
  Sector_t sector = clusterStartSector(pos->cluster);
  sector += (m_clusterMask & pos->position) >> m_bytesPerSectorShift;
  uint8_t* cache = dataCachePrepare(sector, options);
  return cache ? cache + (pos->position & m_sectorMask) : nullptr;
}
//------------------------------------------------------------------------------
// return -1 error, 0 EOC, 1 OK
int8_t ExFatPartition::dirSeek(DirPos_t* pos, uint32_t offset) {
  int8_t status;
  uint32_t tmp = (m_clusterMask & pos->position) + offset;
  pos->position += offset;
  tmp >>= bytesPerClusterShift();
  while (tmp--) {
    if (pos->isContiguous) {
      pos->cluster++;
    } else {
      status = fatGet(pos->cluster, &pos->cluster);
      if (status != 1) {
        return status;
      }
    }
  }
  return 1;
}
//------------------------------------------------------------------------------
// return -1 error, 0 EOC, 1 OK
int8_t ExFatPartition::fatGet(Cluster_t cluster, Cluster_t* value) {
  const uint8_t* cache;
  Cluster_t next;
  Sector_t sector;

  if (cluster > (m_clusterCount + 1)) {
    DBG_FAIL_MACRO;
    return -1;
  }
  sector = m_fatStartSector + (cluster >> (m_bytesPerSectorShift - 2));

  cache = dataCachePrepare(sector, FsCache::CACHE_FOR_READ);
  if (!cache) {
    return -1;
  }
  next = getLe32(cache + ((cluster << 2) & m_sectorMask));
  if (next == EXFAT_EOC) {
    return 0;
  }
  *value = next;
  return 1;
}
//------------------------------------------------------------------------------
bool ExFatPartition::fatPut(Cluster_t cluster, Cluster_t value) {
  Sector_t sector;
  uint8_t* cache;
  if (cluster < 2 || cluster > (m_clusterCount + 1)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  sector = m_fatStartSector + (cluster >> (m_bytesPerSectorShift - 2));
  cache = dataCachePrepare(sector, FsCache::CACHE_FOR_WRITE);
  if (!cache) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  setLe32(cache + ((cluster << 2) & m_sectorMask), value);
#if USE_EXFAT_DUAL_FAT
  if (m_numberOfFats == 2) {
    txRecord(m_txFat, &m_txFatCount, cluster >> (m_bytesPerSectorShift - 2));
  }
#endif  // USE_EXFAT_DUAL_FAT
  return true;

fail:
  return false;
}
//------------------------------------------------------------------------------
bool ExFatPartition::freeChain(Cluster_t cluster) {
  Cluster_t next;
  Cluster_t start = cluster;
  int8_t status;
  do {
    status = fatGet(cluster, &next);
    if (status < 0) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    if (!fatPut(cluster, 0)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    if (status == 0 || (cluster + 1) != next) {
      if (!bitmapModify(start, cluster - start + 1, 0)) {
        DBG_FAIL_MACRO;
        goto fail;
      }
      start = next;
    }
    cluster = next;
  } while (status);

  return true;

fail:
  return false;
}
#if USE_EXFAT_DUAL_FAT
//------------------------------------------------------------------------------
void ExFatPartition::txRecord(uint32_t* list, uint16_t* count, uint32_t off) {
  if (m_txOverflow) {
    return;
  }
  for (uint16_t i = 0; i < *count; i++) {
    if (list[i] == off) {
      return;  // already tracked this transaction
    }
  }
  if (*count >= kMaxTxSectors) {
    m_txOverflow = true;  // fall back to a full reseed at commit
    return;
  }
  list[(*count)++] = off;
}
//------------------------------------------------------------------------------
// Highest cluster number marked used in a bitmap (scans high→low). Returns
// m_clusterCount-1 (force full copy) on read error — safe, never under-reports.
uint32_t ExFatPartition::bitmapHighWater(Sector_t bmpStart, uint32_t bmpSectors) {
  uint8_t buf[m_bytesPerSector];
  for (int32_t s = (int32_t)bmpSectors - 1; s >= 0; s--) {
    if (!m_blockDev->readSector(bmpStart + s, buf)) {
      return m_clusterCount ? m_clusterCount - 1 : 0;
    }
    for (int32_t b = (int32_t)m_bytesPerSector - 1; b >= 0; b--) {
      if (buf[b]) {
        int bit = 7;
        while (!(buf[b] & (1 << bit))) bit--;
        uint32_t idx = (uint32_t)s * m_bytesPerSector * 8 + (uint32_t)b * 8 + bit;
        return idx + 2;  // bitmap bit i == cluster i+2
      }
    }
  }
  return 2;  // bitmap empty (the metadata clusters are always allocated)
}
//------------------------------------------------------------------------------
bool ExFatPartition::reseedInactive() {
  // Copy the ACTIVE (committed) FAT + bitmap onto the INACTIVE (working) copy so
  // the working copy starts equal to committed. Only the [0 .. high-water] range
  // is copied: beyond the highest used cluster both copies are all-zero. The
  // high-water spans BOTH copies (working may hold uncommitted allocations the
  // reseed must discard), and a torn bitmap can only inflate it, so this is safe.
  // Bulk-copy in chunks to amortize per-sector SPI command overhead: a
  // sector-at-a-time reseed is command-latency bound on a large, data-full card
  // (observed ~88 s on a 28 GB card). 32-sector ops cut the transaction count ~32x.
  static const uint32_t kChunkSectors = 32;
  static uint8_t chunk[kChunkSectors * 512];  // 16 KB .bss (DMA-capable internal RAM)
  uint8_t working = 1 - m_activeFat;
  Sector_t spc = (Sector_t)1 << m_sectorsPerClusterShift;
  Sector_t bmpSrc = m_clusterHeapStartSector + (Sector_t)m_activeFat * spc;
  Sector_t bmpDst = m_clusterHeapStartSector + (Sector_t)working * spc;
  uint32_t bmpBytes = (m_clusterCount + 7) / 8;
  uint32_t bmpSectors = (bmpBytes + m_bytesPerSector - 1) >> m_bytesPerSectorShift;

  uint32_t hw = bitmapHighWater(bmpSrc, bmpSectors);
  uint32_t hwW = bitmapHighWater(bmpDst, bmpSectors);
  if (hwW > hw) hw = hwW;
  if (hw >= m_clusterCount) hw = m_clusterCount - 1;

  // FAT entry for cluster C lives in FAT sector C >> (bytesPerSectorShift - 2).
  uint32_t fatSectors = ((hw + 1) >> (m_bytesPerSectorShift - 2)) + 1;
  if (fatSectors > m_fatLength) fatSectors = m_fatLength;
  Sector_t fatSrc = m_fatBaseSector + (Sector_t)m_activeFat * m_fatLength;
  Sector_t fatDst = m_fatBaseSector + (Sector_t)working * m_fatLength;
  for (uint32_t i = 0; i < fatSectors; ) {
    uint32_t n = (fatSectors - i) < kChunkSectors ? (fatSectors - i) : kChunkSectors;
    if (!m_blockDev->readSectors(fatSrc + i, chunk, n) ||
        !m_blockDev->writeSectors(fatDst + i, chunk, n)) {
      return false;
    }
    i += n;
  }
  // Bitmap bit for cluster C lives in bitmap sector (C-2) >> (bytesPerSectorShift+3).
  uint32_t bmpUsed = ((hw - 2) >> (m_bytesPerSectorShift + 3)) + 1;
  if (bmpUsed > bmpSectors) bmpUsed = bmpSectors;
  for (uint32_t i = 0; i < bmpUsed; ) {
    uint32_t n = (bmpUsed - i) < kChunkSectors ? (bmpUsed - i) : kChunkSectors;
    if (!m_blockDev->readSectors(bmpSrc + i, chunk, n) ||
        !m_blockDev->writeSectors(bmpDst + i, chunk, n)) {
      return false;
    }
    i += n;
  }
  m_txFatCount = 0;
  m_txBmpCount = 0;
  m_txOverflow = false;
  return true;
}
#endif  // USE_EXFAT_DUAL_FAT
//------------------------------------------------------------------------------
bool ExFatPartition::commit() {
#if USE_EXFAT_DUAL_FAT
  if (m_numberOfFats == 2) {
  // 1. Flush the working FAT/bitmap/dir/data to the working (inactive) copy.
  if (!cacheSync()) {
    DBG_FAIL_MACRO;
    return false;
  }
  // 2. Atomic commit: flip the ActiveFat bit (VolumeFlags bit 0) with a single
  //    boot-sector write. The boot checksum excludes VolumeFlags, so no recompute.
  pbs_t* pbs = reinterpret_cast<pbs_t*>(
      dataCachePrepare(m_volBootSector, FsCache::CACHE_FOR_READ));
  if (!pbs) {
    DBG_FAIL_MACRO;
    return false;
  }
  BpbExFat_t* bpb = reinterpret_cast<BpbExFat_t*>(pbs->bpb);
  // Atomic commit: toggle the ActiveFat bit in a single boot-sector write. The
  // boot checksum excludes VolumeFlags, so no checksum recompute is needed.
  setLe16(bpb->volumeFlags, getLe16(bpb->volumeFlags) ^ 1);
  dataCacheDirty();
  if (!dataCacheSync() || !syncDevice()) {
    DBG_FAIL_MACRO;
    return false;
  }
  // 3. Roles swapped: committed := old working. Repoint to the new working copy.
  m_activeFat ^= 1;
  uint8_t working = 1 - m_activeFat;
  Sector_t spc = (Sector_t)1 << m_sectorsPerClusterShift;
  m_fatStartSector = m_fatBaseSector + (Sector_t)working * m_fatLength;
  m_bitmapStartSector = m_clusterHeapStartSector + (Sector_t)working * spc;
  // 4. Resync the new working copy from the new committed copy so they match.
  if (m_txOverflow) {
    if (!reseedInactive()) {
      DBG_FAIL_MACRO;
      return false;
    }
  } else {
    uint8_t buf[m_bytesPerSector];
    Sector_t fatC = m_fatBaseSector + (Sector_t)m_activeFat * m_fatLength;
    Sector_t fatW = m_fatBaseSector + (Sector_t)working * m_fatLength;
    for (uint16_t i = 0; i < m_txFatCount; i++) {
      if (!m_blockDev->readSector(fatC + m_txFat[i], buf) ||
          !m_blockDev->writeSector(fatW + m_txFat[i], buf)) {
        DBG_FAIL_MACRO;
        return false;
      }
    }
    Sector_t bmpC = m_clusterHeapStartSector + (Sector_t)m_activeFat * spc;
    Sector_t bmpW = m_clusterHeapStartSector + (Sector_t)working * spc;
    for (uint16_t i = 0; i < m_txBmpCount; i++) {
      if (!m_blockDev->readSector(bmpC + m_txBmp[i], buf) ||
          !m_blockDev->writeSector(bmpW + m_txBmp[i], buf)) {
        DBG_FAIL_MACRO;
        return false;
      }
    }
    m_txFatCount = 0;
    m_txBmpCount = 0;
  }
  // 5. Drop cached working sectors — their addresses moved with the flip.
  dataCacheInvalidate();
#if USE_EXFAT_BITMAP_CACHE
  m_bitmapCache.invalidate();
#endif
    return true;
  }  // m_numberOfFats == 2
#endif  // USE_EXFAT_DUAL_FAT
  return cacheSync();  // stock single-FAT exFAT (or non-dual-FAT build)
}
//------------------------------------------------------------------------------
Cluster_t ExFatPartition::freeClusterCount() {
  Cluster_t nc = 0;
  Sector_t sector = m_clusterHeapStartSector;
  Cluster_t usedCount = 0;
  const uint8_t* cache;

  while (true) {
    cache = dataCachePrepare(sector++, FsCache::CACHE_FOR_READ);
    if (!cache) {
      return -1;
    }
    for (size_t i = 0; i < m_bytesPerSector; i++) {
      if (cache[i] == 0XFF) {
        usedCount += 8;
      } else if (cache[i]) {
        for (uint8_t mask = 1; mask; mask <<= 1) {
          if ((mask & cache[i])) {
            usedCount++;
          }
        }
      }
      nc += 8;
      if (nc >= m_clusterCount) {
        return m_clusterCount - usedCount;
      }
    }
  }
}
//------------------------------------------------------------------------------
bool ExFatPartition::init(FsBlockDevice* dev, uint8_t part,
                          Sector_t startSector) {
  pbs_t* pbs;
  const BpbExFat_t* bpb;
  const MbrSector_t* mbr;
  m_fatType = 0;
  m_blockDev = dev;
  cacheInit(m_blockDev);
  // if part == 0 assume super floppy with FAT boot sector in sector zero
  // if part > 0 assume mbr volume with partition table
  if (part) {
    if (part > 4) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    mbr = reinterpret_cast<MbrSector_t*>(
        dataCachePrepare(0, FsCache::CACHE_FOR_READ));
    if (!mbr) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    const MbrPart_t* mp = mbr->part + part - 1;
    if (mp->type == 0 || (mp->boot != 0 && mp->boot != 0X80)) {
      DBG_FAIL_MACRO;
      goto fail;
    }
    startSector = getLe32(mp->startSector);
  }
  pbs = reinterpret_cast<pbs_t*>(
      dataCachePrepare(startSector, FsCache::CACHE_FOR_READ));
  if (!pbs) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  if (strncmp(pbs->oemName, "EXFAT", 5)) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  bpb = reinterpret_cast<BpbExFat_t*>(pbs->bpb);
  if (bpb->bytesPerSectorShift != m_bytesPerSectorShift) {
    DBG_FAIL_MACRO;
    goto fail;
  }
  m_fatStartSector = startSector + getLe32(bpb->fatOffset);
  m_fatLength = getLe32(bpb->fatLength);
  m_clusterHeapStartSector = startSector + getLe32(bpb->clusterHeapOffset);
  m_clusterCount = getLe32(bpb->clusterCount);
  m_rootDirectoryCluster = getLe32(bpb->rootDirectoryCluster);
  m_sectorsPerClusterShift = bpb->sectorsPerClusterShift;
  m_bytesPerCluster = 1UL << (m_bytesPerSectorShift + m_sectorsPerClusterShift);
  m_clusterMask = m_bytesPerCluster - 1;
  // Allocation bitmap #0 is the first cluster of the heap (cluster 2).
  m_bitmapStartSector = m_clusterHeapStartSector;
#if USE_EXFAT_DUAL_FAT
  // dual-FAT: the ACTIVE copy (ActiveFat bit, VolumeFlags bit 0) is the committed
  // state a host reads. SdFat works on the INACTIVE copy and commits with an
  // atomic flip. Point reads/writes at the inactive copy and seed it from active
  // (a crash mid-resync could have left it partial).
  m_volBootSector = startSector;
  m_fatBaseSector = m_fatStartSector;
  m_numberOfFats = bpb->numberOfFats;
  m_activeFat = getLe16(bpb->volumeFlags) & 1;
  if (m_numberOfFats == 2) {
    uint8_t working = 1 - m_activeFat;
    m_fatStartSector = m_fatBaseSector + (Sector_t)working * m_fatLength;
    m_bitmapStartSector = m_clusterHeapStartSector +
                          ((Sector_t)working << m_sectorsPerClusterShift);
    // The inactive (working) copy must start equal to the committed state. After
    // an abrupt power loss it may hold uncommitted — possibly torn — sectors, so
    // always reseed it from the committed copy. (A VolumeDirty "clean" fast-path
    // is unsafe here: a logger almost always loses power mid-transaction, when the
    // working copy is dirty even though no resync was in flight.)
    if (!reseedInactive()) {
      DBG_FAIL_MACRO;
      goto fail;
    }
  }
#endif  // USE_EXFAT_DUAL_FAT
  // Set m_bitmapStart to first free cluster.
  m_bitmapStart = 0;
  bitmapFind(0, 1);
  m_fatType = FAT_TYPE_EXFAT;
  return true;

fail:
  return false;
}
//------------------------------------------------------------------------------
uint32_t ExFatPartition::rootLength() {
  uint32_t nc = chainSize(m_rootDirectoryCluster);
  return nc << bytesPerClusterShift();
}
