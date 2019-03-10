#include <iostream>
#include <memory>
#include <string>
#include <dlfcn.h>
#include <fstream>
#include <grpcpp/grpcpp.h>

#include "tihu.grpc.pb.h"
#include "../../src/tihu.h"

using grpc::Server;
using grpc::ServerWriter;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;


TIHU_CALLBACK_RETURN tihu_cb(TIHU_CALLBACK_MESSAGE message, long l_param, long w_param, void* user_data) {
  auto writer = (ServerWriter<tihu::SpeakReply>*)user_data;

  if (message == TIHU_WAVE_BUFFER) {
    tihu::SpeakReply reply;
    reply.set_wave((void*)l_param, w_param);
    writer->Write(reply);
  }
}

// Logic and data behind the server's behavior.
class TihuServiceImpl final : public tihu::Tihu::Service {
  public:
    TihuServiceImpl(std::string _lib, std::string _log):
      lib(_lib.c_str()),
      handle(nullptr),
      tihu_Init(nullptr),
      tihu_Close(nullptr),
      tihu_Callback(nullptr),
      tihu_Speak(nullptr) {

        fs.open(_log, std::fstream::out | std::fstream::app);

        InitTihu();
    }

    ~TihuServiceImpl() {
      CloseTihu();
    }

    void CloseTihu() {
      if (handle) {
        try {
          log("Try to close Tihu.");

          tihu_Close();
          dlclose(handle);
          handle = nullptr;
        } catch(std::exception& e) {
          log("Tihu crashed on closing. ", e.what());
        }
      }
    }

    void InitTihu() {
      char* error;

      handle = dlopen(lib, RTLD_LAZY);
      if(!handle) {
          log("Error: Tihu module cannot open.");
          return;
      }

      tihu_Init = (TIHU_PROC_INIT)dlsym(handle, "tihu_Init");
      tihu_Close = (TIHU_PROC_CLOSE)dlsym(handle, "tihu_Close");
      tihu_Callback = (TIHU_PROC_CALLBACK)dlsym(handle, "tihu_Callback");
      tihu_Speak = (TIHU_PROC_SPEAK)dlsym(handle, "tihu_Speak");

      if( tihu_Init == NULL ||
          tihu_Close == NULL ||
          tihu_Callback == NULL ||
          tihu_Speak == NULL)  {
          log("Error: Tihu module cannot load.");
          return;
      }

      if (!tihu_Init()) {
          log("Error: Tihu module cannot init.");
          return;
      }

      log("Tihu loaded successfully.");
    }

  private:
    Status Speak(ServerContext* context, const tihu::SpeakRequest* request,
                    ServerWriter<tihu::SpeakReply>* writer) override {
      try {
        log("Try to speak: " + request->text());
        tihu_Callback(tihu_cb, (void*)writer);
        tihu_Speak(request->text().c_str(), TIHU_VOICE_MBROLA_FEMALE);
        writer->WriteLast(tihu::SpeakReply(), grpc::WriteOptions());
      } catch(std::exception& e) {
        log("Tihu crashed on speaking. ", e.what());
        CloseTihu();
        InitTihu();
      }
      return Status::OK;
    }

    void log(std::string message, const char* reason = "") {
        fs << message << reason << std::endl;
        printf("%s %s\n", message.c_str(), reason);
    }

  private:
    const char* lib;
    void* handle;
    TIHU_PROC_INIT tihu_Init;
    TIHU_PROC_CLOSE tihu_Close;
    TIHU_PROC_CALLBACK tihu_Callback;
    TIHU_PROC_SPEAK tihu_Speak;
    std::fstream fs;
};

void RunServer(std::string addr, std::string lib, std::string log) {
  std::string server_address(addr);
  TihuServiceImpl service(lib, log);

  ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to an *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<Server> server(builder.BuildAndStart());
  std::cout << "Server listening on " << server_address << std::endl;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
  service.CloseTihu();
}

int main(int argc, char** argv) {
  RunServer(argv[1], argv[2], argv[3]);

  return 0;
}