// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef CHROME_BROWSER_EXTENSIONS_SYSTEM_INFO_PROVIDER_H_
#define CHROME_BROWSER_EXTENSIONS_SYSTEM_INFO_PROVIDER_H_

#include <queue>

#include "base/bind.h"
#include "base/callback.h"
#include "base/lazy_instance.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/sequenced_worker_pool.h"
#include "content/public/browser/browser_thread.h"

namespace extensions {

// A generic template for all kinds of system information providers. Each kind
// of SystemInfoProvider is a single shared instance. It is created if needed,
// and destroyed at exit time. This is done via LazyInstance and scoped_ptr.
//
// The SystemInfoProvider is designed to query system information on the worker
// pool. It also maintains a queue of callbacks on the UI thread which are
// waiting for the completion of querying operation. Once the query operation
// is completed, all pending callbacks in the queue get called on the UI
// thread. In this way, it avoids frequent querying operation in case of lots
// of query requests, e.g. calling systemInfo.cpu.get repeatedly in an
// extension process.
//
// Template parameter T is the system information type. It could be the
// structure type generated by IDL parser.
template<class T>
class SystemInfoProvider {
 public:
  // Callback type for completing to get information. The callback accepts
  // two arguments. The first one is the information got already, the second
  // one indicates whether its contents are valid, for example, no error
  // occurs in querying the information.
  typedef base::Callback<void(const T&, bool)> QueryInfoCompletionCallback;
  typedef std::queue<QueryInfoCompletionCallback> CallbackQueue;

  SystemInfoProvider()
    : is_waiting_for_completion_(false) {
    worker_pool_token_ =
      content::BrowserThread::GetBlockingPool()->GetSequenceToken();
  }

  virtual ~SystemInfoProvider() {}

  // Static method to get the single shared instance. Should be implemented
  // in the impl file for each kind of provider.
  static SystemInfoProvider<T>* Get();

  // For testing
  static void InitializeForTesting(SystemInfoProvider<T>* provider) {
    DCHECK(provider != NULL);
    single_shared_provider_.Get().reset(provider);
  }

  // Start to query the system information. Should be called on UI thread.
  // The |callback| will get called once the query is completed.
  void StartQueryInfo(const QueryInfoCompletionCallback& callback) {
    DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));
    DCHECK(!callback.is_null());

    callbacks_.push(callback);

    if (is_waiting_for_completion_)
      return;

    is_waiting_for_completion_ = true;

    base::SequencedWorkerPool* worker_pool =
        content::BrowserThread::GetBlockingPool();
    // The query task posted to the worker pool won't block shutdown, and any
    // running query task at shutdown time will be ignored.
    worker_pool->PostSequencedWorkerTaskWithShutdownBehavior(
          worker_pool_token_,
          FROM_HERE,
          base::Bind(&SystemInfoProvider<T>::QueryOnWorkerPool,
                     base::Unretained(this)),
          base::SequencedWorkerPool::CONTINUE_ON_SHUTDOWN);
  }

  // Query the system information synchronously and output the result to the
  // |info| parameter. The |info| contents MUST be reset firstly in its
  // platform specific implementation. Return true if it succeeds, otherwise
  // false is returned.
  virtual bool QueryInfo(T* info) = 0;

 protected:
  virtual void QueryOnWorkerPool() {
    bool success = QueryInfo(&info_);
    content::BrowserThread::PostTask(content::BrowserThread::UI, FROM_HERE,
        base::Bind(&SystemInfoProvider<T>::OnQueryCompleted,
        base::Unretained(this), success));
  }

  // Called on UI thread. The |success| parameter means whether it succeeds
  // to get the information.
  virtual void OnQueryCompleted(bool success) {
    DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

    while (!callbacks_.empty()) {
      QueryInfoCompletionCallback callback = callbacks_.front();
      callback.Run(info_, success);
      callbacks_.pop();
    }

    is_waiting_for_completion_ = false;
 }

  // Template function for creating the single shared provider instance.
  // Template paramter I is the type of SystemInfoProvider implementation.
  template<class I>
  static SystemInfoProvider<T>* GetInstance() {
    if (!single_shared_provider_.Get().get()) {
      I* impl = new I();
      single_shared_provider_.Get().reset(impl);
    }
    return single_shared_provider_.Get().get();
  }

  // The latest information filled up by QueryInfo implementation. Here we
  // assume the T is disallowed to copy constructor, aligns with the structure
  // type generated by IDL parser.
  T info_;

 private:
  // The single shared provider instance. We create it only when needed.
  static typename base::LazyInstance<
      scoped_ptr<SystemInfoProvider<T> > > single_shared_provider_;

  // The queue of callbacks waiting for the info querying completion. It is
  // maintained on the UI thread.
  CallbackQueue callbacks_;

  // Indicates if it is waiting for the querying completion.
  bool is_waiting_for_completion_;

  // Unqiue sequence token so that the operation of querying inforation can
  // be executed in order.
  base::SequencedWorkerPool::SequenceToken worker_pool_token_;

  DISALLOW_COPY_AND_ASSIGN(SystemInfoProvider<T>);
};

// Static member intialization.
template<class T>
typename base::LazyInstance<scoped_ptr<SystemInfoProvider<T> > >
  SystemInfoProvider<T>::single_shared_provider_ = LAZY_INSTANCE_INITIALIZER;

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_SYSTEM_INFO_PROVIDER_H_
