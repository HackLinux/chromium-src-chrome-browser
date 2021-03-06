// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// This file provides task related API functions.

#ifndef CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_PRIVATE_API_TASKS_H_
#define CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_PRIVATE_API_TASKS_H_

#include <vector>

#include "base/memory/scoped_ptr.h"
#include "chrome/browser/chromeos/extensions/file_manager/private_api_base.h"
#include "chrome/browser/chromeos/file_manager/file_tasks.h"
#include "chrome/common/extensions/api/file_browser_private.h"

namespace base {
class FilePath;
}  // namespace base

namespace extensions {

namespace app_file_handler_util {
class MimeTypeCollector;
}  // namespace app_file_handler_util

// Implements the chrome.fileBrowserPrivate.executeTask method.
class FileBrowserPrivateExecuteTaskFunction
    : public LoggedAsyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("fileBrowserPrivate.executeTask",
                             FILEBROWSERPRIVATE_EXECUTETASK)

 protected:
  virtual ~FileBrowserPrivateExecuteTaskFunction() {}

  // AsyncExtensionFunction overrides.
  virtual bool RunAsync() OVERRIDE;

 private:
  void OnTaskExecuted(
      extensions::api::file_browser_private::TaskResult success);
};

// Implements the chrome.fileBrowserPrivate.getFileTasks method.
class FileBrowserPrivateGetFileTasksFunction
    : public LoggedAsyncExtensionFunction {
 public:
  FileBrowserPrivateGetFileTasksFunction();

  DECLARE_EXTENSION_FUNCTION("fileBrowserPrivate.getFileTasks",
                             FILEBROWSERPRIVATE_GETFILETASKS)

 protected:
  virtual ~FileBrowserPrivateGetFileTasksFunction();

  // AsyncExtensionFunction overrides.
  virtual bool RunAsync() OVERRIDE;

 private:
  void OnMimeTypesCollected(scoped_ptr<std::vector<std::string> > mime_types);

  void OnSniffingMimeTypeCompleted(
      scoped_ptr<app_file_handler_util::PathAndMimeTypeSet> path_mime_set,
      scoped_ptr<std::vector<GURL> > file_urls);

  scoped_ptr<app_file_handler_util::MimeTypeCollector> collector_;
  std::vector<GURL> file_urls_;
  std::vector<base::FilePath> local_paths_;
};

// Implements the chrome.fileBrowserPrivate.setDefaultTask method.
class FileBrowserPrivateSetDefaultTaskFunction
    : public ChromeSyncExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("fileBrowserPrivate.setDefaultTask",
                             FILEBROWSERPRIVATE_SETDEFAULTTASK)

 protected:
  virtual ~FileBrowserPrivateSetDefaultTaskFunction() {}

  // SyncExtensionFunction overrides.
  virtual bool RunSync() OVERRIDE;
};

}  // namespace extensions

#endif  // CHROME_BROWSER_CHROMEOS_EXTENSIONS_FILE_MANAGER_PRIVATE_API_TASKS_H_
