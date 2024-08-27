#pragma once

#include <function2.hpp>
#include <string>

namespace Framework::Scripting {
    class ServerEngine;
    class ClientEngine;

    template <typename EngineKind>
    class SDKRegisterWrapper final {
      private:
        EngineKind *_engine = nullptr;

      public:
        SDKRegisterWrapper(EngineKind *engine): _engine(engine) {}

        EngineKind *GetEngine() const {
            return _engine;
        }
    };
    using SDKRegisterCallback = fu2::function<void(SDKRegisterWrapper<ServerEngine>)>;
    using SDKClientRegisterCallback = fu2::function<void(SDKRegisterWrapper<ClientEngine>)>;
}
