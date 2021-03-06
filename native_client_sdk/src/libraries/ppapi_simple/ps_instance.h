/* Copyright (c) 2013 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#ifndef PPAPI_SIMPLE_PS_INSTANCE_H_
#define PPAPI_SIMPLE_PS_INSTANCE_H_

#include <map>

#include "ppapi/c/pp_instance.h"
#include "ppapi/c/pp_stdint.h"
#include "ppapi/c/ppb_core.h"
#include "ppapi/c/ppb_var.h"
#include "ppapi/c/ppb_view.h"

#include "ppapi/cpp/fullscreen.h"
#include "ppapi/cpp/graphics_3d_client.h"
#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/message_loop.h"
#include "ppapi/cpp/mouse_lock.h"

#include "ppapi/utility/completion_callback_factory.h"

#include "ppapi_simple/ps_event.h"
#include "ppapi_simple/ps_main.h"

#include "sdk_util/thread_safe_queue.h"


typedef std::map<std::string, std::string> PropertyMap_t;

// The basic instance class which also inherits the MouseLock and
// Graphics3DClient interfaces.
class PSInstance : public pp::Instance, pp::MouseLock, pp::Graphics3DClient {
 public:
  enum Verbosity {
    PSV_SILENT,
    PSV_ERROR,
    PSV_WARN,
    PSV_LOG,
  };

  // Returns a pointer to the global instance
  static PSInstance* GetInstance();

  PSInstance(PP_Instance inst, const char *argv[]);
  virtual ~PSInstance();

  // Set a function which will be called on a new thread once initialized.
  // NOTE:  This must be called by the Factory, once Init is called, this
  // function will have no effect.
  void SetMain(PSMainFunc_t func);

  // Returns value based on KEY or default.
  const char* GetProperty(const char* key, const char* def = NULL);

  // Started on Init, a thread which can be safely blocked.
  virtual int MainThread(int argc, char* argv[]);

  // Logging Functions
  void SetVerbosity(Verbosity verbosity);
  void Log(const char *fmt, ...);
  void Warn(const char *fmt, ...);
  void Error(const char *fmt, ...);

  // Event Functions
  void SetEnabledEvents(uint32_t mask);
  void PostEvent(PSEventType type);
  void PostEvent(PSEventType type, PP_Bool bool_value);
  void PostEvent(PSEventType type, PP_Resource resource);
  void PostEvent(PSEventType type, const PP_Var& var);

  PSEvent* TryAcquireEvent();
  PSEvent* WaitAcquireEvent();
  void ReleaseEvent(PSEvent* event);

 protected:
  // Callback functions triggered by Pepper
  //
  // These functions are called on the main pepper thread, so they must
  // not block.
  //
  // Called by the browser when the NaCl module is loaded and all ready to go.
  // This function will create a new thread which will run the pseudo main.
  virtual bool Init(uint32_t argc, const char* argn[], const char* argv[]);

  // Called whenever the in-browser window changes size, it will pass a
  // context change request to whichever thread is handling rendering.
  virtual void DidChangeView(const pp::View& view);

  // Called by the browser when the NaCl canvas gets or loses focus.
  virtual void DidChangeFocus(bool has_focus);

  // Called by the browser to handle the postMessage() call in Javascript.
  virtual void HandleMessage(const pp::Var& message);

  // Called by the browser to handle incoming input events.  Events are Q'd
  // and can later be processed on a sperate processing thread.
  virtual bool HandleInputEvent(const pp::InputEvent& event);

  // Called by the browser when the 3D context is lost.
  virtual void Graphics3DContextLost();

  // Called by the browser when the mouselock is lost.
  virtual void MouseLockLost();

  // Called by Init to processes default and embed tag arguments prior to
  // launching the 'ppapi_main' thread.
  virtual bool ProcessProperties();

 private:
  static void* MainThreadThunk(void *start_info);

 protected:
  pp::MessageLoop* main_loop_;

  PropertyMap_t properties_;
  ThreadSafeQueue<PSEvent> event_queue_;
  uint32_t events_enabled_;
  Verbosity verbosity_;

  PSMainFunc_t main_cb_;

  friend class PSGraphics3DClient;
  friend class PSMouseLock;
};

#endif  // PPAPI_MAIN_PS_INSTANCE_H_

