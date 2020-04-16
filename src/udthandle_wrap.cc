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

#include "node.h"
#include "ngx-queue.h"
#include "udthandle_wrap.h"

namespace httpp {


using v8::Array;
using v8::Context;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Handle;
using v8::HandleScope;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::Persistent;
using v8::String;
using v8::TryCatch;
using v8::Value;

// defined in node.cc
extern ngx_queue_t handle_wrap_queue;

void UDTHandleWrap::Initialize(Handle<Object> target) {
  /* Doesn't do anything at the moment. */
}

void UDTHandleWrap::Ref(const v8::FunctionCallbackInfo<v8::Value>& args) {
  Isolate* isolate = args.GetIsolate();

  UDTHandleWrap* wrap;
  UNWRAP(UDTHandleWrap)

  if (IsAlive(wrap)) uv_ref(wrap->GetHandle());
}

void UDTHandleWrap::Unref(const v8::FunctionCallbackInfo<v8::Value>& args) {
  Isolate* isolate = args.GetIsolate();

  UDTHandleWrap* wrap;
  UNWRAP(UDTHandleWrap)

  if (IsAlive(wrap)) uv_unref(wrap->GetHandle());
}


void UDTHandleWrap::Close(const v8::FunctionCallbackInfo<v8::Value>& args) {
  Isolate* isolate = args.GetIsolate();

  UDTHandleWrap* wrap = static_cast<UDTHandleWrap*>(
      args.Holder()->GetAlignedPointerFromInternalField(0));

  // guard against uninitialized handle or double close
  if (wrap && wrap->handle_) {
    assert(!wrap->object_.IsEmpty());
    uvudt_close((uvudt_t *)wrap->handle_, OnClose);
    wrap->handle_ = NULL;
  }
}

UDTHandleWrap::UDTHandleWrap(Environment* env, Handle<Object> object,
                             uv_handle_t* h) {
  unref_ = false;
  handle_ = h;
  if (h) {
    h->data = this;
  }
  this->envp = env;

  HandleScope scope(env->isolate());

  assert(object_.IsEmpty());
  assert(object->InternalFieldCount() > 0);
  object_ = v8::Persistent<v8::Object>::New(scope, object);
  object_->SetPointerInInternalField(0, this);
  ngx_queue_insert_tail(&handle_wrap_queue, &handle_wrap_queue_);
}

void UDTHandleWrap::SetHandle(uv_handle_t* h) {
  handle_ = h;
  h->data = this;
}


UDTHandleWrap::~UDTHandleWrap() {
  assert(object_.IsEmpty());
  ngx_queue_remove(&handle_wrap_queue_);
}


void UDTHandleWrap::OnClose(uv_handle_t* handle) {
  UDTHandleWrap* wrap = static_cast<UDTHandleWrap*>(handle->data);

  // The wrap object should still be there.
  assert(wrap->object_.IsEmpty() == false);

  // But the handle pointer should be gone.
  assert(wrap->handle_ == NULL);

  wrap->object_->SetPointerInInternalField(0, NULL);
  wrap->object_.Dispose();
  wrap->object_.Clear();

  delete wrap;
}


}  // namespace node
