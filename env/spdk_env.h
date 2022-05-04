//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
#pragma once

#include <atomic>
#include <map>
#include <string>
#include <vector>
#include <iostream>
#include "rocksdb/env.h"
#include "rocksdb/status.h"
#include "port/port.h"
#include "util/mutexlock.h"
#include "env/io_spdk.h"

namespace rocksdb {

class SpdkEnv : public FileSystemWrapper { //EnvWrapper {
 public:
  explicit SpdkEnv(const std::shared_ptr<FileSystem>& base_fs);

  explicit SpdkEnv(const std::shared_ptr<FileSystem>& base_fs, Env* base_env, std::string pcie_addr, int open_mod);//const Options &opt, int open_mod);

  virtual ~SpdkEnv();


  //virtual void SpanDBMigration(std::vector<LiveFileMetaData> files_metadata) override;

  // Partial implementation of the Env interface.
  virtual IOStatus NewSequentialFile(const std::string& fname,
                                   const FileOptions& file_opts,
                                   std::unique_ptr<FSSequentialFile>* result,
                                   IODebugContext* dbg) override;

  virtual IOStatus NewRandomAccessFile(const std::string& fname,
                                     const FileOptions& file_opts,
                                     std::unique_ptr<FSRandomAccessFile>* result,
                                     IODebugContext* dbg) override;

/*  virtual Status NewRandomRWFile(const std::string& fname,
                                 std::unique_ptr<RandomRWFile>* result,
                                 const EnvOptions& options) override;
*/
  virtual IOStatus ReuseWritableFile(const std::string& fname,
                                   const std::string& old_fname,
                                   const FileOptions& file_opts,
                                   std::unique_ptr<FSWritableFile>* result,
                                   IODebugContext* dbg) override;

  virtual IOStatus NewWritableFile(const std::string& fname,
                                 const FileOptions& file_opts,
                                 std::unique_ptr<FSWritableFile>* result,
                                 IODebugContext* dbg) override;

  virtual IOStatus NewWritableFile(const std::string& fname,
                                 std::unique_ptr<FSWritableFile>* result,
                                 uint64_t pre_allocate_size);// override;

  virtual IOStatus NewDirectory(const std::string& name,
                              const IOOptions& io_opts,
                              std::unique_ptr<FSDirectory>* result,
                              IODebugContext* dbg) override;

  virtual IOStatus FileExists(const std::string& fname,
                            const IOOptions& options,
                            IODebugContext* dbg) override;

  virtual IOStatus GetChildren(const std::string& dir,
                             const IOOptions& options,
                             std::vector<std::string>* result,
                             IODebugContext* dbg) override;

  IOStatus DeleteFileInternal(const std::string& fname);

  virtual IOStatus DeleteFile(const std::string& fname,
                            const IOOptions& options,
                            IODebugContext* dbg) override;

//  virtual IOStatus Truncate(const std::string& fname, size_t size) override;

  virtual IOStatus CreateDir(const std::string& dirname,
                           const IOOptions& options,
                           IODebugContext* dbg) override;

  virtual IOStatus CreateDirIfMissing(const std::string& dirname,
                                    const IOOptions& options,
                                    IODebugContext* dbg) override;

  virtual IOStatus DeleteDir(const std::string& dirname,
                           const IOOptions& options,
                           IODebugContext* dbg) override;

  virtual IOStatus GetFileSize(const std::string& fname,
                             const IOOptions& options,
                             uint64_t* file_size,
                             IODebugContext* dbg) override;

  virtual IOStatus GetFileModificationTime(const std::string& fname,
                                         const IOOptions& options,
                                         uint64_t* time,
                                         IODebugContext* dbg) override;

  virtual IOStatus RenameFile(const std::string& src,
                            const std::string& target,
                            const IOOptions& options,
                            IODebugContext* dbg) override;

//  virtual IOStatus LinkFile(const std::string& src,
//                          const std::string& target) override;

  virtual IOStatus NewLogger(const std::string& fname,
                           const IOOptions& io_opts,
                           std::shared_ptr<Logger>* result,
                           IODebugContext* dbg) override;

  virtual IOStatus LockFile(const std::string& fname,
                          const IOOptions& options,
                          FileLock** flock,
                          IODebugContext* dbg) override;

  virtual IOStatus UnlockFile(FileLock* flock,
                            const IOOptions& options,
                            IODebugContext* dbg) override;

  virtual IOStatus GetTestDirectory(const IOOptions& options,
                                  std::string* path,
                                  IODebugContext* dbg) override;

  void ResetStat();// override;

  // Doesn't really sleep, just affects output of GetCurrentTime(), NowMicros()
  // and NowNanos()

 private:
  Env* env_;
  std::string NormalizePath(const std::string path);
  void ReadMeta(FileMeta *file_meta, std::string dbpath);

  uint64_t lo_current_lpn_;
  port::Mutex mutex_;
  //const Options options_;
};

}  // namespace rocksdb
