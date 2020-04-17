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

#include "udthandle_wrap.h"
#include "udtstream_wrap.h"
#include "udtreq_wrap.h"

#include <stdlib.h> // abort()
#include <limits.h> // INT_MAX

#define SLAB_SIZE (1024 * 1024)


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
using v8::Buffer;

typedef class UDTReqWrap<uvudt_shutdown_t> UDTShutdownWrap;

class UDTWriteWrap: public UDTReqWrap<uvudt_write_t> {
 public:
  void* operator new(size_t size, char* storage) { return storage; }

  // This is just to keep the compiler happy. It should never be called, since
  // we don't use exceptions in node.
  void operator delete(void* ptr, char* storage) { assert(0); }

 protected:
  // People should not be using the non-placement new and delete operator on a
  // UDTWriteWrap. Ensure this never happens.
  void* operator new (size_t size) { assert(0); };
  void operator delete(void* ptr) { assert(0); };
};

static bool initialized;

void UDTStreamWrap::Initialize(Local<Object> target) {
  if (initialized) return;
  initialized = true;

  ///slab_allocator = new SlabAllocator(SLAB_SIZE);
  ///AtExit(DeleteSlabAllocator, NULL);

  UDTHandleWrap::Initialize(target);

/*
  buffer_sym = NODE_PSYMBOL("buffer");
  bytes_sym = NODE_PSYMBOL("bytes");
  write_queue_size_sym = NODE_PSYMBOL("writeQueueSize");
  onread_sym = NODE_PSYMBOL("onread");
  oncomplete_sym = NODE_PSYMBOL("oncomplete");*/
}


UDTStreamWrap::UDTStreamWrap(Local<Object> object, uvudt_t* stream)
    : UDTHandleWrap(object, (uv_handle_t*)stream) {
  stream_ = stream;
  if (stream) {
    stream->data = this;
  }
}


void UDTStreamWrap::SetHandle(uv_handle_t* h) {
  UDTHandleWrap::SetHandle(h);
  stream_ = (uvudt_t*)h;
  stream_->data = this;
}


void UDTStreamWrap::UpdateWriteQueueSize() {
  ///HandleScope scope;
  object_->Set(write_queue_size_sym, Integer::New(stream_->write_queue_size));
}


void UDTStreamWrap::ReadStart(const v8::FunctionCallbackInfo<v8::Value>& args) {
  ///HandleScope scope;

  UDTStreamWrap *wrap = ObjectWrap::Unwrap<UDTStreamWrap>(args.Holder());

  r = uv_read_start(wrap->stream_, OnAlloc, OnRead);

  // Error starting the tcp.
  ///if (r) SetErrno(uv_last_error(uv_default_loop()));

  ///return scope.Close(Integer::New(r));
}


void UDTStreamWrap::ReadStop(const v8::FunctionCallbackInfo<v8::Value>& args) {
  ///HandleScope scope;

  UDTStreamWrap *wrap = ObjectWrap::Unwrap<UDTStreamWrap>(args.Holder());

  int r = uv_read_stop(wrap->stream_);

  // Error starting the tcp.
  ///if (r) SetErrno(uv_last_error(uv_default_loop()));

  ///return scope.Close(Integer::New(r));
}


void UDTStreamWrap::OnAlloc(uv_handle_t* handle, size_t suggested_size, uv_buf_t* obuf) {
  UDTStreamWrap* wrap = static_cast<UDTStreamWrap*>(handle->data);
  assert(wrap->stream_ == reinterpret_cast<uvudt_t*>(handle));
  char* buf = malloc(suggested_size);
  
  *obuf = uv_buf_init(buf, suggested_size);
}

void UDTStreamWrap::OnRead(uvudt_t *handle, ssize_t nread, uv_buf_t *buf)
{
    ///HandleScope scope;

    UDTStreamWrap *wrap = static_cast<UDTStreamWrap *>(handle->data);

    // We should not be getting this callback if someone as already called
    // uv_close() on the handle.
    assert(wrap->persistent().IsEmpty() == false);

    if (nread < 0)
    {
        // If libuv reports an error or EOF it *may* give us a buffer back. In that
        // case, return the space to the slab.
        if (buf->base != NULL)
        {
            free(buf->base);
            buf->base = NULL;
        }

        ///SetErrno(uv_last_error(uv_default_loop()));
        ///MakeCallback(wrap->persistent(), onread_sym, 0, NULL);
        return;
    }

    assert(buf->base != NULL);

    if (nread == 0)
        return;
    assert(static_cast<size_t>(nread) <= buf->len);

    int argc = 2;
    Local<Value> argv[2] = {Integer::NewFromUnsigned(nread), Nan::NewBuffer(buf->base, buf->len)};

    ///MakeCallback(wrap->persistent(), onread_sym, argc, argv);
}


void UDTStreamWrap::WriteBuffer(const v8::FunctionCallbackInfo<v8::Value>& args) {
  ///HandleScope scope;

  UDTStreamWrap *wrap = ObjectWrap::Unwrap<UDTStreamWrap>(args.Holder());

  // The first argument is a buffer.
  assert(args.Length() >= 1 && Buffer::HasInstance(args[0]));
  Local<Object> buffer_obj = args[0]->ToObject();
  size_t offset = 0;
  size_t length = Buffer::Length(buffer_obj);

  if (length > INT_MAX) {
    uv_err_t err;
    err.code = UV_ENOBUFS;
    SetErrno(err);
    return scope.Close(v8::Null());
  }

  char* storage = new char[sizeof(UDTWriteWrap)];
  UDTWriteWrap* req_wrap = new (storage) UDTWriteWrap();

  req_wrap->persistent()->SetHiddenValue(buffer_sym, buffer_obj);

  uv_buf_t buf;
  buf.base = Buffer::Data(buffer_obj) + offset;
  buf.len = length;

  int r = uv_write(&req_wrap->req_,
                   wrap->stream_,
                   &buf,
                   1,
                   UDTStreamWrap::AfterWrite);

  req_wrap->Dispatched();
  req_wrap->persistent()->Set(bytes_sym, Number::New((uint32_t) length));

  wrap->UpdateWriteQueueSize();

  if (r) {
    SetErrno(uv_last_error(uv_default_loop()));
    req_wrap->~UDTWriteWrap();
    delete[] storage;
    return scope.Close(v8::Null());
  } else {
    return scope.Close(req_wrap->persistent());
  }
}


template <WriteEncoding encoding>
void UDTStreamWrap::WriteStringImpl(const v8::FunctionCallbackInfo<v8::Value>& args) {
  HandleScope scope;
  int r;

  UDTStreamWrap *wrap = ObjectWrap::Unwrap<UDTStreamWrap>(args.Holder());

  if (args.Length() < 1)
    return ThrowTypeError("Not enough arguments");

  Local<String> string = args[0]->ToString();

  // Compute the size of the storage that the string will be flattened into.
  size_t storage_size;
  switch (encoding) {
    case kAscii:
      storage_size = string->Length();
      break;

    case kUtf8:
      if (!(string->MayContainNonAscii())) {
        // If the string has only ascii characters, we know exactly how big
        // the storage should be.
        storage_size = string->Length();
      } else if (string->Length() < 65536) {
        // A single UCS2 codepoint never takes up more than 3 utf8 bytes.
        // Unless the string is really long we just allocate so much space that
        // we're certain the string fits in there entirely.
        // TODO: maybe check handle->write_queue_size instead of string length?
        storage_size = 3 * string->Length();
      } else {
        // The string is really long. Compute the allocation size that we
        // actually need.
        storage_size = string->Utf8Length();
      }
      break;

    case kUcs2:
      storage_size = string->Length() * sizeof(uint16_t);
      break;

    default:
      // Unreachable.
      assert(0);
  }

  if (storage_size > INT_MAX) {
    uv_err_t err;
    err.code = UV_ENOBUFS;
    SetErrno(err);
    return scope.Close(v8::Null());
  }

  char* storage = new char[sizeof(UDTWriteWrap) + storage_size + 15];
  UDTWriteWrap* req_wrap = new (storage) UDTWriteWrap();

  char* data = reinterpret_cast<char*>(ROUND_UP(
      reinterpret_cast<uintptr_t>(storage) + sizeof(UDTWriteWrap), 16));
  size_t data_size;
  switch (encoding) {
  case kAscii:
      data_size = string->WriteAscii(data, 0, -1,
          String::NO_NULL_TERMINATION | String::HINT_MANY_WRITES_EXPECTED);
      break;

    case kUtf8:
      data_size = string->WriteUtf8(data, -1, NULL, node::WRITE_UTF8_FLAGS);
      break;

    case kUcs2: {
      int chars_copied = string->Write((uint16_t*) data, 0, -1,
          String::NO_NULL_TERMINATION | String::HINT_MANY_WRITES_EXPECTED);
      data_size = chars_copied * sizeof(uint16_t);
      break;
    }

    default:
      // Unreachable
      assert(0);
  }

  assert(data_size <= storage_size);

  uv_buf_t buf;
  buf.base = data;
  buf.len = data_size;

  bool ipc_pipe = wrap->stream_->type == UV_NAMED_PIPE &&
                  ((uv_pipe_t*)wrap->stream_)->ipc;

  if (!ipc_pipe) {
    r = uv_write(&req_wrap->req_,
                 wrap->stream_,
                 &buf,
                 1,
                 UDTStreamWrap::AfterWrite);

  } else {
    uvudt_t* send_stream = NULL;

    if (args[1]->IsObject()) {
      Local<Object> send_stream_obj = args[1]->ToObject();
      assert(send_stream_obj->InternalFieldCount() > 0);
      UDTStreamWrap* send_stream_wrap = static_cast<UDTStreamWrap*>(
          send_stream_obj->GetPointerFromInternalField(0));
      send_stream = send_stream_wrap->GetStream();
    }

    r = uv_write2(&req_wrap->req_,
                  wrap->stream_,
                  &buf,
                  1,
                  send_stream,
                  UDTStreamWrap::AfterWrite);
  }

  req_wrap->Dispatched();
  req_wrap->persistent()->Set(bytes_sym, Number::New((uint32_t) data_size));

  wrap->UpdateWriteQueueSize();

  if (r) {
    SetErrno(uv_last_error(uv_default_loop()));
    req_wrap->~UDTWriteWrap();
    delete[] storage;
    return scope.Close(v8::Null());
  } else {
    return scope.Close(req_wrap->persistent());
  }
}


void UDTStreamWrap::WriteAsciiString(const v8::FunctionCallbackInfo<v8::Value>& args) {
  return WriteStringImpl<kAscii>(args);
}


void UDTStreamWrap::WriteUtf8String(const v8::FunctionCallbackInfo<v8::Value>& args) {
  return WriteStringImpl<kUtf8>(args);
}


void UDTStreamWrap::WriteUcs2String(const v8::FunctionCallbackInfo<v8::Value>& args) {
  return WriteStringImpl<kUcs2>(args);
}


void UDTStreamWrap::AfterWrite(uvudt_write_t* req, int status) {
  UDTWriteWrap* req_wrap = (UDTWriteWrap*) req->data;
  UDTStreamWrap* wrap = (UDTStreamWrap*) req->handle->data;

  HandleScope scope;

  // The wrap and request objects should still be there.
  assert(req_wrap->persistent().IsEmpty() == false);
  assert(wrap->persistent().IsEmpty() == false);

  if (status) {
    SetErrno(uv_last_error(uv_default_loop()));
  }

  wrap->UpdateWriteQueueSize();

  Local<Value> argv[] = {
    Integer::New(status),
    Local<Value>::New(wrap->persistent()),
    Local<Value>::New(req_wrap->persistent())
  };

  MakeCallback(req_wrap->persistent(), oncomplete_sym, ARRAY_SIZE(argv), argv);

  req_wrap->~UDTWriteWrap();
  delete[] reinterpret_cast<char*>(req_wrap);
}


void UDTStreamWrap::Shutdown(const v8::FunctionCallbackInfo<v8::Value>& args) {
  HandleScope scope;

  UDTStreamWrap *wrap = ObjectWrap::Unwrap<UDTStreamWrap>(args.Holder());

  UDTShutdownWrap* req_wrap = new UDTShutdownWrap();

  int r = uv_shutdown(&req_wrap->req_, wrap->stream_, AfterShutdown);

  req_wrap->Dispatched();

  if (r) {
    SetErrno(uv_last_error(uv_default_loop()));
    delete req_wrap;
    return scope.Close(v8::Null());
  } else {
    return scope.Close(req_wrap->persistent());
  }
}


void UDTStreamWrap::AfterShutdown(uvudt_shutdown_t* req, int status) {
  UDTReqWrap<uvudt_shutdown_t>* req_wrap = (UDTReqWrap<uvudt_shutdown_t>*) req->data;
  UDTStreamWrap* wrap = (UDTStreamWrap*) req->handle->data;

  // The wrap and request objects should still be there.
  assert(req_wrap->persistent().IsEmpty() == false);
  assert(wrap->persistent().IsEmpty() == false);

  HandleScope scope;

  if (status) {
    SetErrno(uv_last_error(uv_default_loop()));
  }

  Local<Value> argv[3] = {
    Integer::New(status),
    Local<Value>::New(wrap->persistent()),
    Local<Value>::New(req_wrap->persistent())
  };

  MakeCallback(req_wrap->persistent(), oncomplete_sym, ARRAY_SIZE(argv), argv);

  delete req_wrap;
}


}
