#ifndef STORAGE_CC
#define STORAGE_CC

#include <node.h>
#include <node_buffer.h>
#include <nan.h>
#include <string>

#include <locale>
#include <codecvt>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif /* _WIN32 */

#ifdef __APPLE__
#include <sys/uio.h>
#include <sys/mount.h>
#include <errno.h>
#endif /* __APPLE__ */

#ifdef __linux__
#include <errno.h>
#include <sys/vfs.h>
#endif /* __linux__ */

#include "storage.h"

using namespace v8;

#ifdef _WIN32
std::string os_strerror(DWORD code) {
  LPWSTR lpMsgBuf;
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> convert;
  if (FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&lpMsgBuf,
        0, NULL)) {
    std::string msg = convert.to_bytes(lpMsgBuf);
    LocalFree(lpMsgBuf);
    return msg;
  } else {
    return "Unknown error";
  }
#else
std::string os_strerror(int code) {
  return strerror(code);
#endif
}

class GetFreeSpaceWorker: public Nan::AsyncWorker {
public:
  GetFreeSpaceWorker(Nan::Callback *callback, std::string path)
    : Nan::AsyncWorker(callback), path(path) {}
  ~GetFreeSpaceWorker() {}

  void Execute () {
#ifdef _WIN32
    ULARGE_INTEGER total;
    ULARGE_INTEGER free;
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> convert;
    auto wpath = convert.from_bytes(path);
    if (GetDiskFreeSpaceExW(wpath.c_str(), NULL, &total, &free)) {
      this->rcode = 0;
      this->total = total.QuadPart / 1024 / 1024;
      this->free  = free.QuadPart / 1024 / 1024;
    } else {
      this->rcode = GetLastError();
    }
#else
    struct statfs buf;
    if (statfs(path.c_str(), &buf) == 0) {
      this->rcode = 0;
      this->total = (buf.f_bsize * buf.f_blocks) / 1024 / 1024;
      this->free  = (buf.f_bsize * buf.f_bfree) / 1024 / 1024;
    } else {
      this->rcode = errno;
    }
#endif /* _WIN32 */
  }

  virtual void WorkComplete() {
    Nan::HandleScope scope;
    if (rcode == 0) {
      HandleOKCallback();
    } else {
      std::string errmsg = os_strerror(rcode);
#ifdef _WIN32
      v8::Local<Object> errObj = Nan::To<Object>(Nan::Error(errmsg.c_str())).ToLocalChecked();
      Nan::Set(errObj, Nan::New("code").ToLocalChecked(), Nan::New((uint32_t)rcode));
      Nan::Set(errObj, Nan::New("syscall").ToLocalChecked(), Nan::New("GetDiskFreeSpaceEx").ToLocalChecked());
      Nan::Set(errObj, Nan::New("path").ToLocalChecked(), Nan::New(path.c_str()).ToLocalChecked());
#endif
      v8::Local<v8::Value> argv[] = {
#ifdef _WIN32
        errObj
#else
        Nan::ErrnoException(rcode, "statfs", errmsg.c_str(), this->path.c_str())
#endif
      };
      callback->Call(1, argv, async_resource);
    }
    delete callback;
    callback = NULL;
  }

  // Executed when the async work is complete
  // this function will be run inside the main event loop
  // so it is safe to use V8 again
  void HandleOKCallback () {
    Nan::HandleScope scope;

    Local<Value> argv[2];
    argv[0] = Nan::Null();

    Local<Object> return_info = Nan::New<Object>();
    return_info->Set(Nan::New<String>("totalMegaBytes").ToLocalChecked(), Nan::New<v8::Number>((double)total));
    return_info->Set(Nan::New<String>("freeMegaBytes").ToLocalChecked(), Nan::New<v8::Number>((double)free));
    argv[1] = return_info;

    callback->Call(2, argv, async_resource);
  }

private:
#ifdef _WIN32
  DWORD rcode;
#else /* _WIN32 */
  int rcode;
#endif /* _WIN32 */
  uint64_t total;
  uint64_t free;
  std::string path;
};

NAN_MODULE_INIT(InitAll) {
  Nan::Set(target, Nan::New<String>("getPartitionSpace").ToLocalChecked(),
           Nan::GetFunction(Nan::New<FunctionTemplate>(getPartitionSpace)).ToLocalChecked());
}

NODE_MODULE(storage, InitAll)

NAN_METHOD(getPartitionSpace) {
  if (info.Length() < 2) {
    Nan::ThrowError("Two arguments are required");
    return;
  }

  if (! info[0]->IsString()) {
    Nan::ThrowError("Path argument must be a string");
    return;
  }

  if (! info[1]->IsFunction()) {
    Nan::ThrowError("Callback argument must be a function");
    return;
  }

  std::string path = *Nan::Utf8String(info[0]);
  Nan::Callback *callback = new Nan::Callback(Nan::To<Function>(info[1]).ToLocalChecked());
  AsyncQueueWorker(new GetFreeSpaceWorker(callback, path));
  info.GetReturnValue().SetUndefined();
}

#endif /* STORAGE_CC */
