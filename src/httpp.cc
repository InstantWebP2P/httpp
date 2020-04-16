// Copyright tom zhou<appnet.link@gmail.com>, 2012,2020.
//
// httpp.cc, ported from node.cc
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
#include "udtreq_wrap.h"

using namespace v8;

namespace httpp {

ngx_queue_t req_wrap_queue = { &req_wrap_queue, &req_wrap_queue };

static Handle<Value> GetActiveRequests(const v8::FunctionCallbackInfo<v8::Value>& args) {
  HandleScope scope;

  Local<Array> ary = Array::New();
  ngx_queue_t* q = NULL;
  int i = 0;

  ngx_queue_foreach(q, &req_wrap_queue) {
    ReqWrap<uv_req_t>* w = container_of(q, ReqWrap<uv_req_t>, req_wrap_queue_);
    if (w->object_.IsEmpty()) continue;
    ary->Set(i++, w->object_);
  }

  return scope.Close(ary);
}

}  // namespace node
