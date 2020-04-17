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

#include <nan.h>

#include "ngx-queue.h"
#include "udthandle_wrap.h"

namespace httpp {


using v8::Array;
using v8::Context;
using v8::Function;
using v8::FunctionCallbackInfo;
using v8::FunctionTemplate;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::Object;
using Nan::Persistent;
using v8::String;
using v8::TryCatch;
using v8::Value;


void UDTHandleWrap::Initialize(Local<Object> target) {
  /* Doesn't do anything at the moment. */
}

void UDTHandleWrap::Ref(const Nan::FunctionCallbackInfo<v8::Value>& args) {
    UDTHandleWrap *wrap = ObjectWrap::Unwrap<UDTHandleWrap>(args.Holder());

    wrap->Ref();

    if (IsAlive(wrap))
        uv_ref(wrap->GetHandle());
}

void UDTHandleWrap::Unref(const Nan::FunctionCallbackInfo<v8::Value>& args) {
    UDTHandleWrap *wrap = ObjectWrap::Unwrap<UDTHandleWrap>(args.Holder());

    wrap->Unref();

    if (IsAlive(wrap))
        uv_unref(wrap->GetHandle());
}


void UDTHandleWrap::Close(const Nan::FunctionCallbackInfo<v8::Value>& args) {
    UDTHandleWrap *wrap = ObjectWrap::Unwrap<UDTHandleWrap>(args.Holder());

    // guard against uninitialized handle or double close
    if (IsAlive(wrap))
    {
        uvudt_close((uvudt_t *)wrap->handle_, OnClose);
        wrap->handle_ = NULL;
    }
}

UDTHandleWrap::UDTHandleWrap(v8::Local<v8::Object> object,
                             uv_handle_t* h)
{
    Wrap(object);

    handle_ = h;
    if (h)
    {
        h->data = this;
    }
}

void UDTHandleWrap::SetHandle(uv_handle_t* h) {
  handle_ = h;
  h->data = this;
}

UDTHandleWrap::~UDTHandleWrap() {

}


void UDTHandleWrap::OnClose(uv_handle_t* handle) {
  UDTHandleWrap* wrap = static_cast<UDTHandleWrap*>(handle->data);

  // The wrap object should still be there.
  assert(!wrap->persistent().IsEmpty());

  // But the handle pointer should be gone.
  assert(wrap->handle_ == NULL);

  delete wrap;
}

}  // namespace httpp
