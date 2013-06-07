// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_MEDIA_GALLERIES_FILEAPI_NATIVE_MEDIA_FILE_UTIL_H_
#define CHROME_BROWSER_MEDIA_GALLERIES_FILEAPI_NATIVE_MEDIA_FILE_UTIL_H_

#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "webkit/browser/fileapi/async_file_util.h"

namespace chrome {

// This class handles native file system operations with media type filtering.
// To support virtual file systems it implements the AsyncFileUtil interface
// from scratch and provides synchronous override points.
class NativeMediaFileUtil : public fileapi::AsyncFileUtil {
 public:
  NativeMediaFileUtil();
  virtual ~NativeMediaFileUtil();

  // Uses the MIME sniffer code, which actually looks into the file,
  // to determine if it is really a media file (to avoid exposing
  // non-media files with a media file extension.)
  static base::PlatformFileError IsMediaFile(const base::FilePath& path);

  // AsyncFileUtil overrides.
  virtual bool CreateOrOpen(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      int file_flags,
      const CreateOrOpenCallback& callback) OVERRIDE;
  virtual bool EnsureFileExists(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      const EnsureFileExistsCallback& callback) OVERRIDE;
  virtual bool CreateDirectory(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      bool exclusive,
      bool recursive,
      const StatusCallback& callback) OVERRIDE;
  virtual bool GetFileInfo(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      const GetFileInfoCallback& callback) OVERRIDE;
  virtual bool ReadDirectory(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      const ReadDirectoryCallback& callback) OVERRIDE;
  virtual bool Touch(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      const base::Time& last_access_time,
      const base::Time& last_modified_time,
      const StatusCallback& callback) OVERRIDE;
  virtual bool Truncate(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      int64 length,
      const StatusCallback& callback) OVERRIDE;
  virtual bool CopyFileLocal(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& src_url,
      const fileapi::FileSystemURL& dest_url,
      const StatusCallback& callback) OVERRIDE;
  virtual bool MoveFileLocal(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& src_url,
      const fileapi::FileSystemURL& dest_url,
      const StatusCallback& callback) OVERRIDE;
  virtual bool CopyInForeignFile(
      fileapi::FileSystemOperationContext* context,
      const base::FilePath& src_file_path,
      const fileapi::FileSystemURL& dest_url,
      const StatusCallback& callback) OVERRIDE;
  virtual bool DeleteFile(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      const StatusCallback& callback) OVERRIDE;
  virtual bool DeleteDirectory(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      const StatusCallback& callback) OVERRIDE;
  virtual bool CreateSnapshotFile(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      const CreateSnapshotFileCallback& callback) OVERRIDE;

 protected:
  virtual void CreateDirectoryOnTaskRunnerThread(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      bool exclusive,
      bool recursive,
      const StatusCallback& callback);
  virtual void GetFileInfoOnTaskRunnerThread(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      const GetFileInfoCallback& callback);
  virtual void ReadDirectoryOnTaskRunnerThread(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      const ReadDirectoryCallback& callback);
  virtual void CopyOrMoveFileLocalOnTaskRunnerThread(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& src_url,
      const fileapi::FileSystemURL& dest_url,
      bool copy,
      const StatusCallback& callback);
  virtual void CopyInForeignFileOnTaskRunnerThread(
      fileapi::FileSystemOperationContext* context,
      const base::FilePath& src_file_path,
      const fileapi::FileSystemURL& dest_url,
      const StatusCallback& callback);
  virtual void DeleteDirectoryOnTaskRunnerThread(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      const StatusCallback& callback);
  virtual void CreateSnapshotFileOnTaskRunnerThread(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      const CreateSnapshotFileCallback& callback);

  // The following methods should only be called on the task runner thread.

  // Necessary for copy/move to succeed.
  virtual base::PlatformFileError CreateDirectorySync(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      bool exclusive,
      bool recursive);
  virtual base::PlatformFileError CopyOrMoveFileSync(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& src_url,
      const fileapi::FileSystemURL& dest_url,
      bool copy);
  virtual base::PlatformFileError CopyInForeignFileSync(
      fileapi::FileSystemOperationContext* context,
      const base::FilePath& src_file_path,
      const fileapi::FileSystemURL& dest_url);
  virtual base::PlatformFileError GetFileInfoSync(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      base::PlatformFileInfo* file_info,
      base::FilePath* platform_path);
  // Called by GetFileInfoSync. Meant to be overridden by subclasses that
  // have special mappings from URLs to platform paths (virtual filesystems).
  virtual base::PlatformFileError GetLocalFilePath(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& file_system_url,
      base::FilePath* local_file_path);
  virtual base::PlatformFileError ReadDirectorySync(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      EntryList* file_list);
  // Necessary for move to succeed.
  virtual base::PlatformFileError DeleteDirectorySync(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url);
  virtual base::PlatformFileError CreateSnapshotFileSync(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& url,
      base::PlatformFileInfo* file_info,
      base::FilePath* platform_path,
      scoped_refptr<webkit_blob::ShareableFileReference>* file_ref);

 private:
  // Like GetLocalFilePath(), but always take media_path_filter() into
  // consideration. If the media_path_filter() check fails, return
  // PLATFORM_FILE_ERROR_SECURITY. |local_file_path| does not have to exist.
  base::PlatformFileError GetFilteredLocalFilePath(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& file_system_url,
      base::FilePath* local_file_path);

  // Like GetLocalFilePath(), but if the file does not exist, then return
  // |failure_error|.
  // If |local_file_path| is a file, then take media_path_filter() into
  // consideration.
  // If the media_path_filter() check fails, return |failure_error|.
  // If |local_file_path| is a directory, return PLATFORM_FILE_OK.
  base::PlatformFileError GetFilteredLocalFilePathForExistingFileOrDirectory(
      fileapi::FileSystemOperationContext* context,
      const fileapi::FileSystemURL& file_system_url,
      base::PlatformFileError failure_error,
      base::FilePath* local_file_path);


  base::WeakPtrFactory<NativeMediaFileUtil> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(NativeMediaFileUtil);
};

}  // namespace chrome

#endif  // CHROME_BROWSER_MEDIA_GALLERIES_FILEAPI_NATIVE_MEDIA_FILE_UTIL_H_
