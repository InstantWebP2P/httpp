// Copyright tom zhou<zs68j2ee@gmail.com>, 2012.
//
// udt_wrap.cc, ported from tcp_wrap.cc with udt transport binding
//

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
#include "node_buffer.h"
#include "node_internals.h"
#include "req_wrap.h"
#include "handle_wrap.h"
#include "stream_wrap.h"
#include "udt_wrap.h"

#include <stdlib.h>

// Temporary hack: libuv should provide uv_inet_pton and uv_inet_ntop.
#if defined(__MINGW32__) || defined(_MSC_VER)
  extern "C" {
#   include <inet_net_pton.h>
#   include <inet_ntop.h>
  }
# define uv_inet_pton ares_inet_pton
# define uv_inet_ntop ares_inet_ntop

#else // __POSIX__
# include <arpa/inet.h>
# define uv_inet_pton inet_pton
# define uv_inet_ntop inet_ntop
#endif

namespace node {

using v8::Arguments;
using v8::Context;
using v8::Function;
using v8::FunctionTemplate;
using v8::Handle;
using v8::HandleScope;
using v8::Integer;
using v8::Local;
using v8::Object;
using v8::Null;
using v8::Persistent;
using v8::String;
using v8::TryCatch;
using v8::Undefined;
using v8::Value;

static Persistent<Function> udtConstructor;
static Persistent<String> oncomplete_sym;
static Persistent<String> onconnection_sym;


typedef class ReqWrap<uv_shutdown_t> ShutdownWrap;

class WriteWrap: public ReqWrapWrite<uv_udt_write_t> {
 public:
  void* operator new(size_t size, char* storage) { return storage; }

  // This is just to keep the compiler happy. It should never be called, since
  // we don't use exceptions in node.
  void operator delete(void* ptr, char* storage) { assert(0); }

 protected:
  // People should not be using the non-placement new and delete operator on a
  // WriteWrap. Ensure this never happens.
  void* operator new (size_t size) { assert(0); };
  void operator delete(void* ptr) { assert(0); };
};

typedef class ReqWrapConnect<uv_udt_connect_t> ConnectWrap;

#ifdef _WIN32
static Local<Object> AddressToJS(const sockaddr* addr, const SOCKET fd, const int udtfd);
#else
static Local<Object> AddressToJS(const sockaddr* addr, const int fd, const int udtfd);
#endif

Local<Object> UDTWrap::Instantiate() {
  // If this assert fire then process.binding('udt_wrap') hasn't been
  // called yet.
  assert(udtConstructor.IsEmpty() == false);

  HandleScope scope;
  Local<Object> obj = udtConstructor->NewInstance();

  return scope.Close(obj);
}


void UDTWrap::Initialize(Handle<Object> target) {
  HandleWrap::Initialize(target);
  StreamWrap::Initialize(target);

  HandleScope scope;

  Local<FunctionTemplate> t = FunctionTemplate::New(New);
  t->SetClassName(String::NewSymbol("UDT"));

  t->InstanceTemplate()->SetInternalFieldCount(1);

  NODE_SET_PROTOTYPE_METHOD(t, "close", Close);

  NODE_SET_PROTOTYPE_METHOD(t, "readStart", ReadStart);
  NODE_SET_PROTOTYPE_METHOD(t, "readStop", ReadStop);
  NODE_SET_PROTOTYPE_METHOD(t, "shutdown", Shutdown);

  NODE_SET_PROTOTYPE_METHOD(t, "writeBuffer", WriteBuffer);
  NODE_SET_PROTOTYPE_METHOD(t, "writeAsciiString", WriteAsciiString);
  NODE_SET_PROTOTYPE_METHOD(t, "writeUtf8String", WriteUtf8String);
  NODE_SET_PROTOTYPE_METHOD(t, "writeUcs2String", WriteUcs2String);

  NODE_SET_PROTOTYPE_METHOD(t, "bind", Bind);
  NODE_SET_PROTOTYPE_METHOD(t, "listen", Listen);
  NODE_SET_PROTOTYPE_METHOD(t, "connect", Connect);
  NODE_SET_PROTOTYPE_METHOD(t, "bind6", Bind6);
  NODE_SET_PROTOTYPE_METHOD(t, "bindfd", Bindfd);
  NODE_SET_PROTOTYPE_METHOD(t, "connect6", Connect6);
  NODE_SET_PROTOTYPE_METHOD(t, "getsockname", GetSockName);
  NODE_SET_PROTOTYPE_METHOD(t, "getpeername", GetPeerName);
  NODE_SET_PROTOTYPE_METHOD(t, "setNoDelay", SetNoDelay);
  NODE_SET_PROTOTYPE_METHOD(t, "setKeepAlive", SetKeepAlive);
  NODE_SET_PROTOTYPE_METHOD(t, "setSocketRendez", SetSocketRendez);

  udtConstructor = Persistent<Function>::New(t->GetFunction());

  onconnection_sym = NODE_PSYMBOL("onconnection");
  oncomplete_sym = NODE_PSYMBOL("oncomplete");

  target->Set(String::NewSymbol("UDT"), udtConstructor);
}


Handle<Value> UDTWrap::Close(const Arguments& args) {
  HandleScope scope;

  HandleWrap *wrap = static_cast<HandleWrap*>(
      args.Holder()->GetPointerFromInternalField(0));

  // guard against uninitialized handle or double close
  if (wrap && wrap->handle__) {
    assert(!wrap->object_.IsEmpty());
    uv_udt_close(wrap->handle__, OnClose);
    wrap->handle__ = NULL;
  }

  return v8::Null();
}


Handle<Value> UDTWrap::ReadStart(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)


  int r = uv_udt_read_start(wrap->stream_, OnAlloc, OnRead);

  // Error starting the tcp.
  if (r) SetErrno(uv_last_error(uv_default_loop()));

  return scope.Close(Integer::New(r));
}


Handle<Value> UDTWrap::ReadStop(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  int r = uv_udt_read_stop(wrap->stream_);

  // Error starting the tcp.
  if (r) SetErrno(uv_last_error(uv_default_loop()));

  return scope.Close(Integer::New(r));
}


Handle<Value> UDTWrap::Shutdown(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  ShutdownWrap* req_wrap = new ShutdownWrap();

  int r = uv_udt_shutdown(&req_wrap->req_, wrap->stream_, AfterShutdown);

  req_wrap->Dispatched();

  if (r) {
    SetErrno(uv_last_error(uv_default_loop()));
    delete req_wrap;
    return scope.Close(v8::Null());
  } else {
    return scope.Close(req_wrap->object_);
  }
}


Handle<Value> UDTWrap::WriteBuffer(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

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

  char* storage = new char[sizeof(WriteWrap)];
  WriteWrap* req_wrap = new (storage) WriteWrap();

  req_wrap->object_->SetHiddenValue(buffer_sym, buffer_obj);

  uv_buf_t buf;
  buf.base = Buffer::Data(buffer_obj) + offset;
  buf.len = length;

  int r = uv_udt_write(
		  &req_wrap->req_,
		  wrap->stream_,
		  &buf,
		  1,
		  UDTWrap::AfterWrite);

  req_wrap->Dispatched();
  req_wrap->object_->Set(bytes_sym, Number::New((uint32_t) length));

  wrap->UpdateWriteQueueSize();

  if (r) {
    SetErrno(uv_last_error(uv_default_loop()));
    req_wrap->~WriteWrap();
    delete[] storage;
    return scope.Close(v8::Null());
  } else {
    return scope.Close(req_wrap->object_);
  }
}


template <WriteEncoding encoding>
Handle<Value> UDTWrap::WriteStringImpl(const Arguments& args) {
  HandleScope scope;
  int r;

  UNWRAP(StreamWrap)

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

  char* storage = new char[sizeof(WriteWrap) + storage_size + 15];
  WriteWrap* req_wrap = new (storage) WriteWrap();

  char* data = reinterpret_cast<char*>(ROUND_UP(
      reinterpret_cast<uintptr_t>(storage) + sizeof(WriteWrap), 16));
  size_t data_size;
  switch (encoding) {
  case kAscii:
      data_size = string->WriteAscii(data, 0, -1,
          String::NO_NULL_TERMINATION | String::HINT_MANY_WRITES_EXPECTED);
      break;

    case kUtf8:
      data_size = string->WriteUtf8(data, -1, NULL,
          String::NO_NULL_TERMINATION | String::HINT_MANY_WRITES_EXPECTED);
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

  r = uv_write(
		  &req_wrap->req_,
		  wrap->stream_,
		  &buf,
		  1,
		  StreamWrap::AfterWrite);

  req_wrap->Dispatched();
  req_wrap->object_->Set(bytes_sym, Number::New((uint32_t) data_size));

  wrap->UpdateWriteQueueSize();

  if (r) {
    SetErrno(uv_last_error(uv_default_loop()));
    req_wrap->~WriteWrap();
    delete[] storage;
    return scope.Close(v8::Null());
  } else {
    return scope.Close(req_wrap->object_);
  }
}


Handle<Value> UDTWrap::WriteAsciiString(const Arguments& args) {
  return WriteStringImpl<kAscii>(args);
}


Handle<Value> UDTWrap::WriteUtf8String(const Arguments& args) {
  return WriteStringImpl<kUtf8>(args);
}


Handle<Value> UDTWrap::WriteUcs2String(const Arguments& args) {
  return WriteStringImpl<kUcs2>(args);
}


void StreamWrap::AfterWrite(uv_write_t* req, int status) {
  WriteWrap* req_wrap = (WriteWrap*) req->data;
  StreamWrap* wrap = (StreamWrap*) req->handle->data;

  HandleScope scope;

  // The wrap and request objects should still be there.
  assert(req_wrap->object_.IsEmpty() == false);
  assert(wrap->object_.IsEmpty() == false);

  if (status) {
    SetErrno(uv_last_error(uv_default_loop()));
  }

  wrap->UpdateWriteQueueSize();

  Local<Value> argv[] = {
    Integer::New(status),
    Local<Value>::New(wrap->object_),
    Local<Value>::New(req_wrap->object_)
  };

  MakeCallback(req_wrap->object_, oncomplete_sym, ARRAY_SIZE(argv), argv);

  req_wrap->~WriteWrap();
  delete[] reinterpret_cast<char*>(req_wrap);
}



uv_buf_t StreamWrap::OnAlloc(uv_handle_t* handle, size_t suggested_size) {
  StreamWrap* wrap = static_cast<StreamWrap*>(handle->data);
  assert(wrap->stream_ == reinterpret_cast<uv_stream_t*>(handle));
  char* buf = slab_allocator->Allocate(wrap->object_, suggested_size);
  return uv_buf_init(buf, suggested_size);
}


void StreamWrap::OnReadCommon(uv_stream_t* handle, ssize_t nread,
    uv_buf_t buf, uv_handle_type pending) {
  HandleScope scope;

  StreamWrap* wrap = static_cast<StreamWrap*>(handle->data);

  // We should not be getting this callback if someone as already called
  // uv_close() on the handle.
  assert(wrap->object_.IsEmpty() == false);

  if (nread < 0)  {
    // If libuv reports an error or EOF it *may* give us a buffer back. In that
    // case, return the space to the slab.
    if (buf.base != NULL) {
      slab_allocator->Shrink(wrap->object_, buf.base, 0);
    }

    SetErrno(uv_last_error(uv_default_loop()));
    MakeCallback(wrap->object_, onread_sym, 0, NULL);
    return;
  }

  assert(buf.base != NULL);
  Local<Object> slab = slab_allocator->Shrink(wrap->object_,
                                              buf.base,
                                              nread);

  if (nread == 0) return;
  assert(static_cast<size_t>(nread) <= buf.len);

  int argc = 3;
  Local<Value> argv[4] = {
    slab,
    Integer::NewFromUnsigned(buf.base - Buffer::Data(slab)),
    Integer::NewFromUnsigned(nread)
  };

  Local<Object> pending_obj;
  if (pending == UV_TCP) {
    pending_obj = UDTWrap::Instantiate();
  } else {
    // We only support sending UV_TCP and UV_NAMED_PIPE right now.
    assert(pending == UV_UNKNOWN_HANDLE);
  }

  if (!pending_obj.IsEmpty()) {
    assert(pending_obj->InternalFieldCount() > 0);
    StreamWrap* pending_wrap =
      static_cast<StreamWrap*>(pending_obj->GetPointerFromInternalField(0));
    if (uv_accept(handle, pending_wrap->GetStream())) abort();
    argv[3] = pending_obj;
    argc++;
  }

  MakeCallback(wrap->object_, onread_sym, argc, argv);
}


void StreamWrap::OnRead(uv_stream_t* handle, ssize_t nread, uv_buf_t buf) {
  OnReadCommon(handle, nread, buf, UV_UNKNOWN_HANDLE);
}


////////////////////////////////////////////////////////////////
UDTWrap* UDTWrap::Unwrap(Local<Object> obj) {
  assert(!obj.IsEmpty());
  assert(obj->InternalFieldCount() > 0);
  return static_cast<UDTWrap*>(obj->GetPointerFromInternalField(0));
}


uv_udt_t* UDTWrap::UVHandle() {
  return &handle_;
}


Handle<Value> UDTWrap::New(const Arguments& args) {
  // This constructor should not be exposed to public javascript.
  // Therefore we assert that we are not trying to call this as a
  // normal function.
  assert(args.IsConstructCall());

  HandleScope scope;
  UDTWrap* wrap = new UDTWrap(args.This());
  assert(wrap);

  return scope.Close(args.This());
}


UDTWrap::UDTWrap(Handle<Object> object)
    : StreamWrap(object, (uv_stream_t*) &handle_) {
  int r = uv_udt_init(uv_default_loop(), &handle_);
  assert(r == 0); // How do we proxy this error up to javascript?
                  // Suggestion: uv_udt_init() returns void.
  UpdateWriteQueueSize();
}


UDTWrap::~UDTWrap() {
  assert(object_.IsEmpty());
}


Handle<Value> UDTWrap::GetSockName(const Arguments& args) {
  HandleScope scope;
  struct sockaddr_storage address;

  UNWRAP(UDTWrap)

  int addrlen = sizeof(address);
  int r = uv_udt_getsockname(&wrap->handle_,
                             reinterpret_cast<sockaddr*>(&address),
                             &addrlen);

  if (r) {
    SetErrno(uv_last_error(uv_default_loop()));
    return Null();
  }

  const sockaddr* addr = reinterpret_cast<const sockaddr*>(&address);
#if defined(WIN32)
  return scope.Close(AddressToJS(addr, wrap->handle_.fd, wrap->handle_.udtfd));
#else
  return scope.Close(AddressToJS(addr, wrap->handle_.stream.fd, wrap->handle_.udtfd));
#endif
}


Handle<Value> UDTWrap::GetPeerName(const Arguments& args) {
  HandleScope scope;
  struct sockaddr_storage address;

  UNWRAP(UDTWrap)

  int addrlen = sizeof(address);
  int r = uv_udt_getpeername(&wrap->handle_,
                             reinterpret_cast<sockaddr*>(&address),
                             &addrlen);

  if (r) {
    SetErrno(uv_last_error(uv_default_loop()));
    return Null();
  }

  const sockaddr* addr = reinterpret_cast<const sockaddr*>(&address);
  return scope.Close(AddressToJS(addr, -1, -1));
}


Handle<Value> UDTWrap::SetNoDelay(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  int enable = static_cast<int>(args[0]->BooleanValue());
  int r = uv_udt_nodelay(&wrap->handle_, enable);
  if (r)
    SetErrno(uv_last_error(uv_default_loop()));

  return Undefined();
}


Handle<Value> UDTWrap::SetKeepAlive(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  int enable = args[0]->Int32Value();
  unsigned int delay = args[1]->Uint32Value();

  int r = uv_udt_keepalive(&wrap->handle_, enable, delay);
  if (r)
    SetErrno(uv_last_error(uv_default_loop()));

  return Undefined();
}


Handle<Value> UDTWrap::Bind(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  String::AsciiValue ip_address(args[0]);
  int port = args[1]->Int32Value();

  struct sockaddr_in address = uv_ip4_addr(*ip_address, port);
  int r = uv_udt_bind(&wrap->handle_, address);

  // Error starting the udt.
  if (r) SetErrno(uv_last_error(uv_default_loop()));

  return scope.Close(Integer::New(r));
}


Handle<Value> UDTWrap::Bind6(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  String::AsciiValue ip6_address(args[0]);
  int port = args[1]->Int32Value();

  struct sockaddr_in6 address = uv_ip6_addr(*ip6_address, port);
  int r = uv_udt_bind6(&wrap->handle_, address);

  // Error starting the udt.
  if (r) SetErrno(uv_last_error(uv_default_loop()));

  return scope.Close(Integer::New(r));
}


// binding UDT socket on existing udp socket/fd
Handle<Value> UDTWrap::Bindfd(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  int udpfd = args[0]->Int32Value();
#if defined(WIN32)
  // windows int to SOCKET fd casting
  // it's valid from 0 to (INVALID_SOCKET - 1)
  if (udpfd == -1) {
      udpfd = INVALID_SOCKET;
  }
#else
  // unix-like os
#endif

  int r = uv_udt_bindfd(&wrap->handle_, udpfd);

  // Error starting the udt.
  if (r) SetErrno(uv_last_error(uv_default_loop()));

  return scope.Close(Integer::New(r));
}


Handle<Value> UDTWrap::Listen(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  int backlog = args[0]->Int32Value();

  int r = uv_listen((uv_stream_t*)&wrap->handle_, backlog, OnConnection);

  // Error starting the udt.
  if (r) SetErrno(uv_last_error(uv_default_loop()));

  return scope.Close(Integer::New(r));
}


void UDTWrap::OnConnection(uv_stream_t* handle, int status) {
  HandleScope scope;

  UDTWrap* wrap = static_cast<UDTWrap*>(handle->data);
  assert(&wrap->handle_ == (uv_udt_t*)handle);

  // We should not be getting this callback if someone as already called
  // uv_close() on the handle.
  assert(wrap->object_.IsEmpty() == false);

  Local<Value> argv[1];

  if (status == 0) {
    // Instantiate the client javascript object and handle.
    Local<Object> client_obj = Instantiate();

    // Unwrap the client javascript object.
    assert(client_obj->InternalFieldCount() > 0);
    UDTWrap* client_wrap =
        static_cast<UDTWrap*>(client_obj->GetPointerFromInternalField(0));

    if (uv_accept(handle, (uv_stream_t*)&client_wrap->handle_)) return;

    // Successful accept. Call the onconnection callback in JavaScript land.
    argv[0] = client_obj;
  } else {
    SetErrno(uv_last_error(uv_default_loop()));
    argv[0] = Local<Value>::New(Null());
  }

  MakeCallback(wrap->object_, onconnection_sym, ARRAY_SIZE(argv), argv);
}


void UDTWrap::AfterConnect(uv_connect_t* req, int status) {
  ConnectWrap* req_wrap = (ConnectWrap*) req->data;
  UDTWrap* wrap = (UDTWrap*) req->handle->data;

  HandleScope scope;

  // The wrap and request objects should still be there.
  assert(req_wrap->object_.IsEmpty() == false);
  assert(wrap->object_.IsEmpty() == false);

  if (status) {
    SetErrno(uv_last_error(uv_default_loop()));
  }

  Local<Value> argv[5] = {
    Integer::New(status),
    Local<Value>::New(wrap->object_),
    Local<Value>::New(req_wrap->object_),
    Local<Value>::New(v8::True()),
    Local<Value>::New(v8::True())
  };

  MakeCallback(req_wrap->object_, oncomplete_sym, ARRAY_SIZE(argv), argv);

  delete req_wrap;
}


Handle<Value> UDTWrap::Connect(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  String::AsciiValue ip_address(args[0]);
  int port = args[1]->Int32Value();

  struct sockaddr_in address = uv_ip4_addr(*ip_address, port);

  // I hate when people program C++ like it was C, and yet I do it too.
  // I'm too lazy to come up with the perfect class hierarchy here. Let's
  // just do some type munging.
  ConnectWrap* req_wrap = new ConnectWrap();

  int r = uv_udt_connect(&req_wrap->req_, &wrap->handle_, address,
      AfterConnect);

  req_wrap->Dispatched();

  if (r) {
    SetErrno(uv_last_error(uv_default_loop()));
    delete req_wrap;
    return scope.Close(v8::Null());
  } else {
    return scope.Close(req_wrap->object_);
  }
}


Handle<Value> UDTWrap::Connect6(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  String::AsciiValue ip_address(args[0]);
  int port = args[1]->Int32Value();

  struct sockaddr_in6 address = uv_ip6_addr(*ip_address, port);

  ConnectWrap* req_wrap = new ConnectWrap();

  int r = uv_udt_connect6(&req_wrap->req_, &wrap->handle_, address,
      AfterConnect);

  req_wrap->Dispatched();

  if (r) {
    SetErrno(uv_last_error(uv_default_loop()));
    delete req_wrap;
    return scope.Close(v8::Null());
  } else {
    return scope.Close(req_wrap->object_);
  }
}


// set socket in rendezvous mode for p2p connection
Handle<Value> UDTWrap::SetSocketRendez(const Arguments& args) {
  HandleScope scope;

  UNWRAP(UDTWrap)

  int enable = args[0]->Int32Value();

  int r = uv_udt_setrendez(&wrap->handle_, enable);
  if (r)
    SetErrno(uv_last_error(uv_default_loop()));

  return Undefined();
}


// also used by udp_wrap.cc
#ifdef _WIN32
static Local<Object> AddressToJS(const sockaddr* addr, const SOCKET fd, const int udtfd) {
#else
static Local<Object> AddressToJS(const sockaddr* addr, const int fd, const int udtfd) {
#endif
  static Persistent<String> address_sym;
  static Persistent<String> family_sym;
  static Persistent<String> port_sym;
  static Persistent<String> ipv4_sym;
  static Persistent<String> ipv6_sym;
  static Persistent<String> fd_sym;
  static Persistent<String> udtfd_sym;

  HandleScope scope;
  char ip[INET6_ADDRSTRLEN];
  const sockaddr_in *a4;
  const sockaddr_in6 *a6;
  int port;

  if (address_sym.IsEmpty()) {
    address_sym = NODE_PSYMBOL("address");
    family_sym = NODE_PSYMBOL("family");
    port_sym = NODE_PSYMBOL("port");
    ipv4_sym = NODE_PSYMBOL("IPv4");
    ipv6_sym = NODE_PSYMBOL("IPv6");
    fd_sym = NODE_PSYMBOL("fd");
    udtfd_sym = NODE_PSYMBOL("udtfd");
  }

  Local<Object> info = Object::New();

  switch (addr->sa_family) {
  case AF_INET6:
    a6 = reinterpret_cast<const sockaddr_in6*>(addr);
    uv_inet_ntop(AF_INET6, &a6->sin6_addr, ip, sizeof ip);
    port = ntohs(a6->sin6_port);
    info->Set(address_sym, String::New(ip));
    info->Set(family_sym, ipv6_sym);
    info->Set(port_sym, Integer::New(port));
#ifdef _WIN32
	if (fd != INVALID_SOCKET) info->Set(fd_sym, Integer::New(fd));
#else
	if (fd != -1) info->Set(fd_sym, Integer::New(fd));
#endif
    if (udtfd != -1) info->Set(udtfd_sym, Integer::New(udtfd));
    break;

  case AF_INET:
    a4 = reinterpret_cast<const sockaddr_in*>(addr);
    uv_inet_ntop(AF_INET, &a4->sin_addr, ip, sizeof ip);
    port = ntohs(a4->sin_port);
    info->Set(address_sym, String::New(ip));
    info->Set(family_sym, ipv4_sym);
    info->Set(port_sym, Integer::New(port));
#ifdef _WIN32
	if (fd != INVALID_SOCKET) info->Set(fd_sym, Integer::New(fd));
#else
	if (fd != -1) info->Set(fd_sym, Integer::New(fd));
#endif
    if (udtfd != -1) info->Set(udtfd_sym, Integer::New(udtfd));
    break;

  default:
    info->Set(address_sym, String::Empty());
  }

  return scope.Close(info);
}


}  // namespace node

NODE_MODULE(node_udt_wrap, node::UDTWrap::Initialize)
