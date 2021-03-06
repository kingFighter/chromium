// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/browser/fileapi/local_file_system_operation.h"

#include "base/bind.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time.h"
#include "net/base/escape.h"
#include "net/url_request/url_request.h"
#include "webkit/browser/fileapi/async_file_util.h"
#include "webkit/browser/fileapi/copy_or_move_operation_delegate.h"
#include "webkit/browser/fileapi/file_observers.h"
#include "webkit/browser/fileapi/file_system_context.h"
#include "webkit/browser/fileapi/file_system_file_util.h"
#include "webkit/browser/fileapi/file_system_mount_point_provider.h"
#include "webkit/browser/fileapi/file_system_operation_context.h"
#include "webkit/browser/fileapi/file_system_task_runners.h"
#include "webkit/browser/fileapi/file_system_url.h"
#include "webkit/browser/fileapi/file_writer_delegate.h"
#include "webkit/browser/fileapi/remove_operation_delegate.h"
#include "webkit/browser/fileapi/sandbox_file_stream_writer.h"
#include "webkit/browser/quota/quota_manager.h"
#include "webkit/common/blob/shareable_file_reference.h"
#include "webkit/common/fileapi/file_system_types.h"
#include "webkit/common/fileapi/file_system_util.h"
#include "webkit/common/quota/quota_types.h"

using webkit_blob::ScopedFile;
using webkit_blob::ShareableFileReference;

namespace fileapi {

namespace {
void NopCloseFileCallback() {}
}

LocalFileSystemOperation::LocalFileSystemOperation(
    const FileSystemURL& url,
    FileSystemContext* file_system_context,
    scoped_ptr<FileSystemOperationContext> operation_context)
    : file_system_context_(file_system_context),
      operation_context_(operation_context.Pass()),
      async_file_util_(NULL),
      peer_handle_(base::kNullProcessHandle),
      pending_operation_(kOperationNone) {
  DCHECK(operation_context_.get());
  operation_context_->DetachUserDataThread();
  async_file_util_ = file_system_context_->GetAsyncFileUtil(url.type());
  DCHECK(async_file_util_);
}

LocalFileSystemOperation::~LocalFileSystemOperation() {
}

void LocalFileSystemOperation::CreateFile(const FileSystemURL& url,
                                          bool exclusive,
                                          const StatusCallback& callback) {
  DCHECK(SetPendingOperationType(kOperationCreateFile));
  GetUsageAndQuotaThenRunTask(
      url,
      base::Bind(&LocalFileSystemOperation::DoCreateFile,
                 AsWeakPtr(), url, callback, exclusive),
      base::Bind(callback, base::PLATFORM_FILE_ERROR_FAILED));
}

void LocalFileSystemOperation::CreateDirectory(const FileSystemURL& url,
                                               bool exclusive,
                                               bool recursive,
                                               const StatusCallback& callback) {
  DCHECK(SetPendingOperationType(kOperationCreateDirectory));
  GetUsageAndQuotaThenRunTask(
      url,
      base::Bind(&LocalFileSystemOperation::DoCreateDirectory,
                 AsWeakPtr(), url, callback, exclusive, recursive),
      base::Bind(callback, base::PLATFORM_FILE_ERROR_FAILED));
}

void LocalFileSystemOperation::Copy(const FileSystemURL& src_url,
                                    const FileSystemURL& dest_url,
                                    const StatusCallback& callback) {
  DCHECK(SetPendingOperationType(kOperationCopy));
  DCHECK(!recursive_operation_delegate_);
  recursive_operation_delegate_.reset(
      new CopyOrMoveOperationDelegate(
          file_system_context(),
          src_url, dest_url,
          CopyOrMoveOperationDelegate::OPERATION_COPY,
          base::Bind(&LocalFileSystemOperation::DidFinishOperation,
                     AsWeakPtr(), callback)));
  recursive_operation_delegate_->RunRecursively();
}

void LocalFileSystemOperation::Move(const FileSystemURL& src_url,
                                    const FileSystemURL& dest_url,
                                    const StatusCallback& callback) {
  DCHECK(SetPendingOperationType(kOperationMove));
  DCHECK(!recursive_operation_delegate_);
  recursive_operation_delegate_.reset(
      new CopyOrMoveOperationDelegate(
          file_system_context(),
          src_url, dest_url,
          CopyOrMoveOperationDelegate::OPERATION_MOVE,
          base::Bind(&LocalFileSystemOperation::DidFinishOperation,
                     AsWeakPtr(), callback)));
  recursive_operation_delegate_->RunRecursively();
}

void LocalFileSystemOperation::DirectoryExists(const FileSystemURL& url,
                                               const StatusCallback& callback) {
  DCHECK(SetPendingOperationType(kOperationDirectoryExists));
  async_file_util_->GetFileInfo(
      operation_context_.Pass(), url,
      base::Bind(&LocalFileSystemOperation::DidDirectoryExists,
                 AsWeakPtr(), callback));
}

void LocalFileSystemOperation::FileExists(const FileSystemURL& url,
                                          const StatusCallback& callback) {
  DCHECK(SetPendingOperationType(kOperationFileExists));
  async_file_util_->GetFileInfo(
      operation_context_.Pass(), url,
      base::Bind(&LocalFileSystemOperation::DidFileExists,
                 AsWeakPtr(), callback));
}

void LocalFileSystemOperation::GetMetadata(
    const FileSystemURL& url, const GetMetadataCallback& callback) {
  DCHECK(SetPendingOperationType(kOperationGetMetadata));
  async_file_util_->GetFileInfo(operation_context_.Pass(), url, callback);
}

void LocalFileSystemOperation::ReadDirectory(
    const FileSystemURL& url, const ReadDirectoryCallback& callback) {
  DCHECK(SetPendingOperationType(kOperationReadDirectory));
  async_file_util_->ReadDirectory(
      operation_context_.Pass(), url, callback);
}

void LocalFileSystemOperation::Remove(const FileSystemURL& url,
                                      bool recursive,
                                      const StatusCallback& callback) {
  DCHECK(SetPendingOperationType(kOperationRemove));
  DCHECK(!recursive_operation_delegate_);
  recursive_operation_delegate_.reset(
      new RemoveOperationDelegate(
          file_system_context(), url,
          base::Bind(&LocalFileSystemOperation::DidFinishOperation,
                     AsWeakPtr(), callback)));
  if (recursive)
    recursive_operation_delegate_->RunRecursively();
  else
    recursive_operation_delegate_->Run();
}

void LocalFileSystemOperation::Write(
    const FileSystemURL& url,
    scoped_ptr<FileWriterDelegate> writer_delegate,
    scoped_ptr<net::URLRequest> blob_request,
    const WriteCallback& callback) {
  DCHECK(SetPendingOperationType(kOperationWrite));
  file_writer_delegate_ = writer_delegate.Pass();
  file_writer_delegate_->Start(
      blob_request.Pass(),
      base::Bind(&LocalFileSystemOperation::DidWrite, AsWeakPtr(),
                 url, callback));
}

void LocalFileSystemOperation::Truncate(const FileSystemURL& url, int64 length,
                                        const StatusCallback& callback) {
  DCHECK(SetPendingOperationType(kOperationTruncate));
  GetUsageAndQuotaThenRunTask(
      url,
      base::Bind(&LocalFileSystemOperation::DoTruncate,
                 AsWeakPtr(), url, callback, length),
      base::Bind(callback, base::PLATFORM_FILE_ERROR_FAILED));
}

void LocalFileSystemOperation::TouchFile(const FileSystemURL& url,
                                         const base::Time& last_access_time,
                                         const base::Time& last_modified_time,
                                         const StatusCallback& callback) {
  DCHECK(SetPendingOperationType(kOperationTouchFile));
  async_file_util_->Touch(
      operation_context_.Pass(), url,
      last_access_time, last_modified_time,
      base::Bind(&LocalFileSystemOperation::DidFinishOperation,
                 AsWeakPtr(), callback));
}

void LocalFileSystemOperation::OpenFile(const FileSystemURL& url,
                                        int file_flags,
                                        base::ProcessHandle peer_handle,
                                        const OpenFileCallback& callback) {
  DCHECK(SetPendingOperationType(kOperationOpenFile));
  peer_handle_ = peer_handle;

  if (file_flags & (
      (base::PLATFORM_FILE_ENUMERATE | base::PLATFORM_FILE_TEMPORARY |
       base::PLATFORM_FILE_HIDDEN))) {
    callback.Run(base::PLATFORM_FILE_ERROR_FAILED,
                 base::kInvalidPlatformFileValue,
                 base::Closure(),
                 base::kNullProcessHandle);
    return;
  }
  GetUsageAndQuotaThenRunTask(
      url,
      base::Bind(&LocalFileSystemOperation::DoOpenFile,
                 AsWeakPtr(),
                 url, callback, file_flags),
      base::Bind(callback, base::PLATFORM_FILE_ERROR_FAILED,
                 base::kInvalidPlatformFileValue,
                 base::Closure(),
                 base::kNullProcessHandle));
}

// We can only get here on a write or truncate that's not yet completed.
// We don't support cancelling any other operation at this time.
void LocalFileSystemOperation::Cancel(const StatusCallback& cancel_callback) {
  DCHECK(cancel_callback_.is_null());
  cancel_callback_ = cancel_callback;

  if (file_writer_delegate_.get()) {
    DCHECK_EQ(kOperationWrite, pending_operation_);
    // This will call DidWrite() with ABORT status code.
    file_writer_delegate_->Cancel();
  } else {
    // For truncate we have no way to cancel the inflight operation (for now).
    // Let it just run and dispatch cancel callback later.
    DCHECK_EQ(kOperationTruncate, pending_operation_);
  }
}

LocalFileSystemOperation*
LocalFileSystemOperation::AsLocalFileSystemOperation() {
  return this;
}

void LocalFileSystemOperation::SyncGetPlatformPath(
    const FileSystemURL& url,
    base::FilePath* platform_path) {
  DCHECK(SetPendingOperationType(kOperationGetLocalPath));
  FileSystemFileUtil* file_util = file_system_context()->GetFileUtil(
      url.type());
  DCHECK(file_util);
  file_util->GetLocalFilePath(operation_context_.get(), url, platform_path);
}

void LocalFileSystemOperation::CreateSnapshotFile(
    const FileSystemURL& url,
    const SnapshotFileCallback& callback) {
  DCHECK(SetPendingOperationType(kOperationCreateSnapshotFile));
  async_file_util_->CreateSnapshotFile(
      operation_context_.Pass(), url, callback);
}

void LocalFileSystemOperation::CopyInForeignFile(
    const base::FilePath& src_local_disk_file_path,
    const FileSystemURL& dest_url,
    const StatusCallback& callback) {
  DCHECK(SetPendingOperationType(kOperationCopyInForeignFile));
  GetUsageAndQuotaThenRunTask(
      dest_url,
      base::Bind(&LocalFileSystemOperation::DoCopyInForeignFile,
                 AsWeakPtr(), src_local_disk_file_path, dest_url,
                 callback),
      base::Bind(callback, base::PLATFORM_FILE_ERROR_FAILED));
}

void LocalFileSystemOperation::RemoveFile(
    const FileSystemURL& url,
    const StatusCallback& callback) {
  DCHECK(SetPendingOperationType(kOperationRemove));
  async_file_util_->DeleteFile(
      operation_context_.Pass(), url,
      base::Bind(&LocalFileSystemOperation::DidFinishOperation,
                 AsWeakPtr(), callback));
}

void LocalFileSystemOperation::RemoveDirectory(
    const FileSystemURL& url,
    const StatusCallback& callback) {
  DCHECK(SetPendingOperationType(kOperationRemove));
  async_file_util_->DeleteDirectory(
      operation_context_.Pass(), url,
      base::Bind(&LocalFileSystemOperation::DidFinishOperation,
                 AsWeakPtr(), callback));
}

void LocalFileSystemOperation::CopyFileLocal(
    const FileSystemURL& src_url,
    const FileSystemURL& dest_url,
    const StatusCallback& callback) {
  DCHECK(SetPendingOperationType(kOperationCopy));
  DCHECK(src_url.IsInSameFileSystem(dest_url));
  GetUsageAndQuotaThenRunTask(
      dest_url,
      base::Bind(&LocalFileSystemOperation::DoCopyFileLocal,
                 AsWeakPtr(), src_url, dest_url, callback),
      base::Bind(callback, base::PLATFORM_FILE_ERROR_FAILED));
}

void LocalFileSystemOperation::MoveFileLocal(
    const FileSystemURL& src_url,
    const FileSystemURL& dest_url,
    const StatusCallback& callback) {
  DCHECK(SetPendingOperationType(kOperationMove));
  DCHECK(src_url.IsInSameFileSystem(dest_url));
  GetUsageAndQuotaThenRunTask(
      dest_url,
      base::Bind(&LocalFileSystemOperation::DoMoveFileLocal,
                 AsWeakPtr(), src_url, dest_url, callback),
      base::Bind(callback, base::PLATFORM_FILE_ERROR_FAILED));
}

void LocalFileSystemOperation::GetUsageAndQuotaThenRunTask(
    const FileSystemURL& url,
    const base::Closure& task,
    const base::Closure& error_callback) {
  quota::QuotaManagerProxy* quota_manager_proxy =
      file_system_context()->quota_manager_proxy();
  if (!quota_manager_proxy ||
      !file_system_context()->GetQuotaUtil(url.type())) {
    // If we don't have the quota manager or the requested filesystem type
    // does not support quota, we should be able to let it go.
    operation_context_->set_allowed_bytes_growth(kint64max);
    task.Run();
    return;
  }

  DCHECK(quota_manager_proxy);
  DCHECK(quota_manager_proxy->quota_manager());
  quota_manager_proxy->quota_manager()->GetUsageAndQuota(
      url.origin(),
      FileSystemTypeToQuotaStorageType(url.type()),
      base::Bind(&LocalFileSystemOperation::DidGetUsageAndQuotaAndRunTask,
                 AsWeakPtr(), task, error_callback));
}

void LocalFileSystemOperation::DidGetUsageAndQuotaAndRunTask(
    const base::Closure& task,
    const base::Closure& error_callback,
    quota::QuotaStatusCode status,
    int64 usage, int64 quota) {
  if (status != quota::kQuotaStatusOk) {
    LOG(WARNING) << "Got unexpected quota error : " << status;
    error_callback.Run();
    return;
  }

  operation_context_->set_allowed_bytes_growth(quota - usage);
  task.Run();
}

void LocalFileSystemOperation::DoCreateFile(
    const FileSystemURL& url,
    const StatusCallback& callback,
    bool exclusive) {
  async_file_util_->EnsureFileExists(
      operation_context_.Pass(), url,
      base::Bind(
          exclusive ?
              &LocalFileSystemOperation::DidEnsureFileExistsExclusive :
              &LocalFileSystemOperation::DidEnsureFileExistsNonExclusive,
          AsWeakPtr(), callback));
}

void LocalFileSystemOperation::DoCreateDirectory(
    const FileSystemURL& url,
    const StatusCallback& callback,
    bool exclusive, bool recursive) {
  async_file_util_->CreateDirectory(
      operation_context_.Pass(),
      url, exclusive, recursive,
      base::Bind(&LocalFileSystemOperation::DidFinishOperation,
                 AsWeakPtr(), callback));
}

void LocalFileSystemOperation::DoCopyFileLocal(
    const FileSystemURL& src_url,
    const FileSystemURL& dest_url,
    const StatusCallback& callback) {
  async_file_util_->CopyFileLocal(
      operation_context_.Pass(), src_url, dest_url,
      base::Bind(&LocalFileSystemOperation::DidFinishOperation,
                 AsWeakPtr(), callback));
}

void LocalFileSystemOperation::DoMoveFileLocal(
    const FileSystemURL& src_url,
    const FileSystemURL& dest_url,
    const StatusCallback& callback) {
  async_file_util_->MoveFileLocal(
      operation_context_.Pass(), src_url, dest_url,
      base::Bind(&LocalFileSystemOperation::DidFinishOperation,
                 AsWeakPtr(), callback));
}

void LocalFileSystemOperation::DoCopyInForeignFile(
    const base::FilePath& src_local_disk_file_path,
    const FileSystemURL& dest_url,
    const StatusCallback& callback) {
  async_file_util_->CopyInForeignFile(
      operation_context_.Pass(),
      src_local_disk_file_path, dest_url,
      base::Bind(&LocalFileSystemOperation::DidFinishOperation,
                 AsWeakPtr(), callback));
}

void LocalFileSystemOperation::DoTruncate(const FileSystemURL& url,
                                          const StatusCallback& callback,
                                          int64 length) {
  async_file_util_->Truncate(
      operation_context_.Pass(), url, length,
      base::Bind(&LocalFileSystemOperation::DidFinishOperation,
                 AsWeakPtr(), callback));
}

void LocalFileSystemOperation::DoOpenFile(const FileSystemURL& url,
                                          const OpenFileCallback& callback,
                                          int file_flags) {
  async_file_util_->CreateOrOpen(
      operation_context_.Pass(), url, file_flags,
      base::Bind(&LocalFileSystemOperation::DidOpenFile,
                 AsWeakPtr(), callback));
}

void LocalFileSystemOperation::DidEnsureFileExistsExclusive(
    const StatusCallback& callback,
    base::PlatformFileError rv, bool created) {
  if (rv == base::PLATFORM_FILE_OK && !created) {
    callback.Run(base::PLATFORM_FILE_ERROR_EXISTS);
  } else {
    DidFinishOperation(callback, rv);
  }
}

void LocalFileSystemOperation::DidEnsureFileExistsNonExclusive(
    const StatusCallback& callback,
    base::PlatformFileError rv, bool /* created */) {
  DidFinishOperation(callback, rv);
}

void LocalFileSystemOperation::DidFinishOperation(
    const StatusCallback& callback,
    base::PlatformFileError rv) {
  if (!cancel_callback_.is_null()) {
    DCHECK_EQ(kOperationTruncate, pending_operation_);

    StatusCallback cancel_callback = cancel_callback_;
    callback.Run(base::PLATFORM_FILE_ERROR_ABORT);
    cancel_callback.Run(base::PLATFORM_FILE_OK);
  } else {
    callback.Run(rv);
  }
}

void LocalFileSystemOperation::DidDirectoryExists(
    const StatusCallback& callback,
    base::PlatformFileError rv,
    const base::PlatformFileInfo& file_info) {
  if (rv == base::PLATFORM_FILE_OK && !file_info.is_directory)
    rv = base::PLATFORM_FILE_ERROR_NOT_A_DIRECTORY;
  callback.Run(rv);
}

void LocalFileSystemOperation::DidFileExists(
    const StatusCallback& callback,
    base::PlatformFileError rv,
    const base::PlatformFileInfo& file_info) {
  if (rv == base::PLATFORM_FILE_OK && file_info.is_directory)
    rv = base::PLATFORM_FILE_ERROR_NOT_A_FILE;
  callback.Run(rv);
}

void LocalFileSystemOperation::DidWrite(
    const FileSystemURL& url,
    const WriteCallback& write_callback,
    base::PlatformFileError rv,
    int64 bytes,
    FileWriterDelegate::WriteProgressStatus write_status) {
  const bool complete = (
      write_status != FileWriterDelegate::SUCCESS_IO_PENDING);
  if (complete && write_status != FileWriterDelegate::ERROR_WRITE_NOT_STARTED) {
    DCHECK(operation_context_);
    operation_context_->change_observers()->Notify(
        &FileChangeObserver::OnModifyFile, MakeTuple(url));
  }

  StatusCallback cancel_callback = cancel_callback_;
  write_callback.Run(rv, bytes, complete);
  if (!cancel_callback.is_null())
    cancel_callback.Run(base::PLATFORM_FILE_OK);
}

void LocalFileSystemOperation::DidOpenFile(
    const OpenFileCallback& callback,
    base::PlatformFileError rv,
    base::PassPlatformFile file,
    bool unused) {
  if (rv == base::PLATFORM_FILE_OK)
    CHECK_NE(base::kNullProcessHandle, peer_handle_);
  callback.Run(rv, file.ReleaseValue(),
               base::Bind(&NopCloseFileCallback),
               peer_handle_);
}

bool LocalFileSystemOperation::SetPendingOperationType(OperationType type) {
  if (pending_operation_ != kOperationNone)
    return false;
  pending_operation_ = type;
  return true;
}

}  // namespace fileapi
