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

using Nan::AsyncWorker;
using namespace v8;

#ifdef _WIN32
static wchar_t errbuf[1024] = {0};
#endif

#ifdef _WIN32
const wchar_t* drive_strerror(int code) {
	if (FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, 0, code, 0, errbuf,
			1024, NULL)) {
		return errbuf;
	} else {
		wcscpy(errbuf, L"Unknown error");
		return errbuf;
	}
#else

const char* drive_strerror(int code) {
	return strerror(code);
#endif
}

class GetFreeSpaceWorker: public AsyncWorker {
 public:
  GetFreeSpaceWorker(Nan::Callback *callback, std::string path)
    : AsyncWorker(callback), path(path) {}
  ~GetFreeSpaceWorker() {}

  void Execute () {
#ifdef _WIN32
      ULARGE_INTEGER total;
      ULARGE_INTEGER free;
      std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> convert;
      auto wpath = convert.from_bytes(path);
      if (GetDiskFreeSpaceExW(wpath.c_str(), NULL, &total, &free)) {
        this->rcode = 0;
        this->total = (unsigned int) (total.QuadPart / 1024 / 1024);
        this->free  = (unsigned int) (free.QuadPart / 1024 / 1024);
      } else {
        this->rcode = GetLastError();
        std::wstring err = drive_strerror(this->rcode);
        this->SetErrorMessage(convert.to_bytes(err).c_str());
      }
#else
      struct statfs buf;
      if (statfs(path.c_str(), &buf) == 0) {
        this->rcode = 0;
        this->total = (buf.f_bsize * buf.f_blocks) / 1024 / 1024;
        this->free  = (buf.f_bsize * buf.f_bfree) / 1024 / 1024;
      } else {
        this->rcode = errno;
        this->SetErrorMessage(drive_strerror(rcode));
      }
#endif /* _WIN32 */
  }

  // Executed when the async work is complete
  // this function will be run inside the main event loop
  // so it is safe to use V8 again
  void HandleOKCallback () {
    Nan::HandleScope scope;

    Local<Value> argv[2];
    argv[0] = Nan::Null();

    Local<Object> return_info = Nan::New<Object>();

    return_info->Set(Nan::New<String>("totalMegaBytes").ToLocalChecked(), Nan::New<Uint32>(total));
    return_info->Set(Nan::New<String>("freeMegaBytes").ToLocalChecked(), Nan::New<Uint32>(free));
    argv[1] = return_info;

    callback->Call(2, argv, async_resource);
  }

 private:
#ifdef _WIN32
	DWORD rcode;
#else /* _WIN32 */
	int rcode;
#endif /* _WIN32 */
	unsigned int total;
	unsigned int free;
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
}

#endif /* STORAGE_CC */
