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
#include "ExFatFile.h"
//==============================================================================
/**
 * \class ExFatVolume
 * \brief exFAT volume.
 */
class ExFatVolume : public ExFatPartition {
 public:
  ExFatVolume() {}
  //----------------------------------------------------------------------------
  /** Get file's user settable attributes.
   * \param[in] path path to file.
   * \return user settable file attributes for success else -1.
   */
  int attrib(const char* path) {
    ExFatFile tmpFile;
    return tmpFile.open(this, path, O_RDONLY) ? tmpFile.attrib() : -1;
  }
  //----------------------------------------------------------------------------
  /** Set file's user settable attributes.
   * \param[in] path path to file.
   * \param[in] bits bit-wise or of selected attributes: FS_ATTRIB_READ_ONLY,
   *            FS_ATTRIB_HIDDEN, FS_ATTRIB_SYSTEM, FS_ATTRIB_ARCHIVE.
   *
   * \return true for success or false for failure.
   */
  bool attrib(const char* path, uint8_t bits) {
    ExFatFile tmpFile;
    return tmpFile.open(this, path, O_RDONLY) ? tmpFile.attrib(bits) : false;
  }
  //----------------------------------------------------------------------------
  /**
   * Initialize an FatVolume object.
   * \param[in] dev Device block driver.
   * \param[in] setCwv Set current working volume if true.
   * \param[in] part Partition to initialize.
   * \param[in] startSector Start sector of volume if part is zero.
   * \return true for success or false for failure.
   */
  bool begin(FsBlockDevice* dev, bool setCwv = true, uint8_t part = 1,
             uint32_t startSector = 0) {
    if (!init(dev, part, startSector)) {
      return false;
    }
    if (!chdir()) {
      return false;
    }
    if (setCwv || !m_cwv) {
      m_cwv = this;
    }
    // NOTE: dual-FAT "free but referenced" healing is NOT done here. A whole-
    // volume scan is O(files) and was ~80 s on a card with months of day-files.
    // Only files that were OPEN for writing at the power loss can be torn, so the
    // caller reconciles just those (reconcileFile) after mount, before any write.
    return true;
  }
#if USE_EXFAT_DUAL_FAT
  //----------------------------------------------------------------------------
  /** Heal one file's "free but referenced" tail in the working bitmap: a torn
   * dual-FAT commit can leave the most-recently-allocated clusters of a file that
   * was OPEN for writing referenced by its dir entry but free in the active
   * bitmap (the ActiveFat flip doesn't cover the shared-heap dir entry). Only
   * such open-at-power-loss files are at risk — cleanly-closed (rotated) files
   * are already consistent — so the caller invokes this for just those files
   * after mount, before any allocation. Earlier clusters were committed in prior
   * syncs, so only a tail margin (far above the writer's sync granularity) is
   * checked. Idempotent; no-op-safe if the file is absent. Returns true. */
  bool reconcileFile(const char* path) {
    if (m_numberOfFats != 2) {
      return true;
    }
    ExFatFile f;
    if (!f.open(this, path, O_RDONLY)) {
      return true;  // not present — nothing to reconcile
    }
    Cluster_t first = f.firstCluster();
    uint64_t  len   = f.dataLength();
    bool      contig = f.isContiguous();
    f.close();
    if (first < 2 || len == 0) {
      return true;
    }
    uint32_t n = (uint32_t)((len + m_bytesPerCluster - 1) >> bytesPerClusterShift());
    uint32_t margin = (uint32_t)((1u << 20) >> bytesPerClusterShift()) + 1;  // ~1 MB tail
    if (margin > n) margin = n;
    if (contig) {
      bitmapMarkUsed(first + n - margin, margin);
    } else {
      // Fragmented: walk the chain keeping the last `margin` clusters in a ring,
      // then mark them. Bounded to this one at-risk file (not the whole volume).
      static const uint32_t kRing = 272;       // >= max margin (1 MB / 4 KB cluster + 1)
      static Cluster_t ring[kRing];
      uint32_t ringN = margin < kRing ? margin : kRing;
      Cluster_t cl = first;
      uint32_t head = 0, walked = 0;
      while (cl >= 2 && cl < 0x7FFFFFFF && walked < m_clusterCount) {
        ring[head % ringN] = cl;
        head++; walked++;
        Cluster_t next;
        if (fatGet(cl, &next) <= 0) break;
        cl = next;
      }
      uint32_t have = head < ringN ? head : ringN;
      for (uint32_t k = 0; k < have; k++) {
        bitmapMarkUsed(ring[(head - have + k) % ringN], 1);
      }
    }
    // A heal may have filled the cluster init() picked as "first free".
    m_bitmapStart = 0;
    bitmapFind(0, 1);
    return true;
  }
#endif  // USE_EXFAT_DUAL_FAT
  //----------------------------------------------------------------------------
  /**
   * Set volume working directory to root.
   * \return true for success or false for failure.
   */
  bool chdir() {
    m_vwd.close();
    return m_vwd.openRoot(this);
  }
  //----------------------------------------------------------------------------
  /**
   * Set volume working directory.
   * \param[in] path Path for volume working directory.
   * \return true for success or false for failure.
   */
  bool chdir(const char* path);
  //----------------------------------------------------------------------------
  /** Change global working volume to this volume. */
  void chvol() { m_cwv = this; }
  //----------------------------------------------------------------------------
  /**
   * Test for the existence of a file.
   *
   * \param[in] path Path of the file to be tested for.
   *
   * \return true if the file exists else false.
   */
  bool exists(const char* path) {
    ExFatFile tmp;
    return tmp.open(this, path, O_RDONLY);
  }
  //----------------------------------------------------------------------------
  /** List the directory contents of the root directory.
   *
   * \param[in] pr Print stream for list.
   *
   * \param[in] flags The inclusive OR of
   *
   * LS_DATE - %Print file modification date
   *
   * LS_SIZE - %Print file size.
   *
   * LS_R - Recursive list of subdirectories.
   *
   * \return true for success or false for failure.
   */
  bool ls(print_t* pr, uint8_t flags = 0) { return m_vwd.ls(pr, flags); }
  //----------------------------------------------------------------------------
  /** List the contents of a directory.
   *
   * \param[in] pr Print stream for list.
   *
   * \param[in] path directory to list.
   *
   * \param[in] flags The inclusive OR of
   *
   * LS_DATE - %Print file modification date
   *
   * LS_SIZE - %Print file size.
   *
   * LS_R - Recursive list of subdirectories.
   *
   * \return true for success or false for failure.
   */
  bool ls(print_t* pr, const char* path, uint8_t flags) {
    ExFatFile dir;
    return dir.open(this, path, O_RDONLY) && dir.ls(pr, flags);
  }
  //----------------------------------------------------------------------------
  /** Make a subdirectory in the volume root directory.
   *
   * \param[in] path A path with a valid 8.3 DOS name for the subdirectory.
   *
   * \param[in] pFlag Create missing parent directories if true.
   *
   * \return true for success or false for failure.
   */
  bool mkdir(const char* path, bool pFlag = true) {
    ExFatFile sub;
    return sub.mkdir(vwd(), path, pFlag);
  }
  //----------------------------------------------------------------------------
  /** open a file
   *
   * \param[in] path location of file to be opened.
   * \param[in] oflag open flags.
   * \return a ExFile object.
   */
  ExFile open(const char* path, oflag_t oflag = O_RDONLY) {
    ExFile tmpFile;
    tmpFile.open(this, path, oflag);
    return tmpFile;
  }
  //----------------------------------------------------------------------------
  /** Remove a file from the volume root directory.
   *
   * \param[in] path A path with a valid 8.3 DOS name for the file.
   *
   * \return true for success or false for failure.
   */
  bool remove(const char* path) {
    ExFatFile tmp;
    return tmp.open(this, path, O_WRONLY) && tmp.remove();
  }
  //----------------------------------------------------------------------------
  /** Rename a file or subdirectory.
   *
   * \param[in] oldPath Path name to the file or subdirectory to be renamed.
   *
   * \param[in] newPath New path name of the file or subdirectory.
   *
   * The \a newPath object must not exist before the rename call.
   *
   * The file to be renamed must not be open.  The directory entry may be
   * moved and file system corruption could occur if the file is accessed by
   * a file object that was opened before the rename() call.
   *
   * \return true for success or false for failure.
   */
  bool rename(const char* oldPath, const char* newPath) {
    ExFatFile file;
    return file.open(vwd(), oldPath, O_RDONLY) && file.rename(vwd(), newPath);
  }
  //----------------------------------------------------------------------------
  /** Remove a subdirectory from the volume's working directory.
   *
   * \param[in] path A path with a valid 8.3 DOS name for the subdirectory.
   *
   * The subdirectory file will be removed only if it is empty.
   *
   * \return true for success or false for failure.
   */
  bool rmdir(const char* path) {
    ExFatFile sub;
    return sub.open(this, path, O_RDONLY) && sub.rmdir();
  }
  //----------------------------------------------------------------------------
  /** Truncate a file to a specified length.  The current file position
   * will be at the new EOF.
   *
   * \param[in] path A path with a valid 8.3 DOS name for the file.
   * \param[in] length The desired length for the file.
   *
   * \return true for success or false for failure.
   */
  bool truncate(const char* path, uint64_t length) {
    ExFatFile file;
    if (!file.open(this, path, O_WRONLY)) {
      return false;
    }
    return file.truncate(length);
  }
#if ENABLE_ARDUINO_SERIAL
  //----------------------------------------------------------------------------
  /** List the directory contents of the root directory to Serial.
   *
   * \return true for success or false for failure.
   */
  bool ls() { return ls(&Serial); }
  //----------------------------------------------------------------------------
  /** List the directory contents of the volume root to Serial.
   *
   * \param[in] flags The inclusive OR of
   *
   * LS_DATE - %Print file modification date
   *
   * LS_SIZE - %Print file size.
   *
   * LS_R - Recursive list of subdirectories.
   *
   * \return true for success or false for failure.
   */
  bool ls(uint8_t flags) { return ls(&Serial, flags); }
  //----------------------------------------------------------------------------
  /** List the directory contents of a directory to Serial.
   *
   * \param[in] path directory to list.
   *
   * \param[in] flags The inclusive OR of
   *
   * LS_DATE - %Print file modification date
   *
   * LS_SIZE - %Print file size.
   *
   * LS_R - Recursive list of subdirectories.
   *
   * \return true for success or false for failure.
   */
  bool ls(const char* path, uint8_t flags = 0) {
    return ls(&Serial, path, flags);
  }
#endif  // ENABLE_ARDUINO_SERIAL
#if ENABLE_ARDUINO_STRING
  //----------------------------------------------------------------------------
  /**
   * Set volume working directory.
   * \param[in] path Path for volume working directory.
   * \return true for success or false for failure.
   */
  bool chdir(const String& path) { return chdir(path.c_str()); }
  //----------------------------------------------------------------------------
  /** Test for the existence of a file in a directory
   *
   * \param[in] path Path of the file to be tested for.
   *
   * \return true if the file exists else false.
   */
  bool exists(const String& path) { return exists(path.c_str()); }
  //----------------------------------------------------------------------------
  /** Make a subdirectory in the volume root directory.
   *
   * \param[in] path A path with a valid 8.3 DOS name for the subdirectory.
   *
   * \param[in] pFlag Create missing parent directories if true.
   *
   * \return true for success or false for failure.
   */
  bool mkdir(const String& path, bool pFlag = true) {
    return mkdir(path.c_str(), pFlag);
  }
  //----------------------------------------------------------------------------
  /** open a file
   *
   * \param[in] path location of file to be opened.
   * \param[in] oflag open oflag flags.
   * \return a ExFile object.
   */
  ExFile open(const String& path, oflag_t oflag = O_RDONLY) {
    return open(path.c_str(), oflag);
  }
  //----------------------------------------------------------------------------
  /** Remove a file from the volume root directory.
   *
   * \param[in] path A path with a valid name for the file.
   *
   * \return true for success or false for failure.
   */
  bool remove(const String& path) { return remove(path.c_str()); }
  //----------------------------------------------------------------------------
  /** Rename a file or subdirectory.
   *
   * \param[in] oldPath Path name to the file or subdirectory to be renamed.
   *
   * \param[in] newPath New path name of the file or subdirectory.
   *
   * The \a newPath object must not exist before the rename call.
   *
   * The file to be renamed must not be open.  The directory entry may be
   * moved and file system corruption could occur if the file is accessed by
   * a file object that was opened before the rename() call.
   *
   * \return true for success or false for failure.
   */
  bool rename(const String& oldPath, const String& newPath) {
    return rename(oldPath.c_str(), newPath.c_str());
  }
  //----------------------------------------------------------------------------
  /** Remove a subdirectory from the volume's working directory.
   *
   * \param[in] path A path with a valid name for the subdirectory.
   *
   * The subdirectory file will be removed only if it is empty.
   *
   * \return true for success or false for failure.
   */
  bool rmdir(const String& path) { return rmdir(path.c_str()); }
  //----------------------------------------------------------------------------
  /** Truncate a file to a specified length.  The current file position
   * will be at the new EOF.
   *
   * \param[in] path A path with a valid name for the file.
   * \param[in] length The desired length for the file.
   *
   * \return true for success or false for failure.
   */
  bool truncate(const String& path, uint64_t length) {
    return truncate(path.c_str(), length);
  }
#endif  // ENABLE_ARDUINO_STRING

 private:
  friend ExFatFile;
  static ExFatVolume* cwv() { return m_cwv; }
  ExFatFile* vwd() { return &m_vwd; }
  static ExFatVolume* m_cwv;
  ExFatFile m_vwd;
};
