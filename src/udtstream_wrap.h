// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef UDTSTREAM_WRAP_H_
#define UDTSTREAM_WRAP_H_

#include <nan.h>

#include "udthandle_wrap.h"

namespace httpp {

using v8::Context;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Isolate;
using v8::Local;
using v8::NewStringType;
using v8::Number;
using v8::Object;
using v8::ObjectTemplate;
using v8::String;
using v8::Value;

class UDTStreamWrap : public UDTHandleWrap {
 public:
  uvudt_t* GetStream() { return stream_; }

  static void Initialize(v8::Local<v8::Object> target);

  // JavaScript functions
  static void ReadStart(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void ReadStop(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void Shutdown(const v8::FunctionCallbackInfo<v8::Value>& args);

  static void WriteBuffer(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void WriteAsciiString(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void WriteUtf8String(const v8::FunctionCallbackInfo<v8::Value>& args);
  static void WriteUcs2String(const v8::FunctionCallbackInfo<v8::Value>& args);

 protected:
  UDTStreamWrap(v8::Local<v8::Object> object, uvudt_t* stream);
  virtual void SetHandle(uvudt_t* h);
  void StateChange() { }
  void UpdateWriteQueueSize();

 private:
  static inline char* NewSlab(v8::Local<v8::Object> global, v8::Local<v8::Object> wrap_obj);

  // Callbacks for libuv
  static void AfterWrite(uvudt_write_t* req, int status);
  static void OnAlloc(uvudt_t* handle, size_t suggested_size, uv_buf_t* buf);
  static void AfterShutdown(uvudt_shutdown_t* req, int status);

  static void OnRead(uvudt_t* handle, ssize_t nread, uv_buf_t* buf);

  template <enum Nan::Encoding encoding>
  static void WriteStringImpl(const v8::FunctionCallbackInfo<v8::Value> &args);

  uvudt_t* stream_;
};


}  // namespace httpp


#endif  // UDTSTREAM_WRAP_H_
