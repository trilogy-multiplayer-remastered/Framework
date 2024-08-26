#pragma once

#include <function2.hpp>
#include <string>

namespace Framework::Scripting {
    class Engine;
    
    class SDKRegisterWrapper final {
      private:
        void *_engine                           = nullptr;

      public:
        SDKRegisterWrapper(void *engine): _engine(engine) {}

        Framework::Scripting::Engine *GetEngine() const {
            return reinterpret_cast<Framework::Scripting::Engine *>(_engine);
        }
    };
    using SDKRegisterCallback = fu2::function<void(SDKRegisterWrapper)>;
}