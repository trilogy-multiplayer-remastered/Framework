/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <map>

#include <logging/logger.h>
#include <utils/time.h>
#include <sol/sol.hpp>

#include "errors.h"
#include "shared.h"

namespace Framework::Scripting {
    class Engine {
      public:
        sol::state _luaEngine;
        // std::map<std::string, std::vector<Callback>> _eventHandlers = {};

      public:
        virtual EngineError Init(SDKRegisterCallback) = 0;
        virtual EngineError Shutdown()                = 0;
        virtual void Update()                         = 0;

        bool InitCommonSDK();

        sol::state& GetLuaEngine() {
            return _luaEngine;
        }

        bool IsEventValid(std::string name) {
            return _luaEngine[name] != nullptr;
        }

        template<typename T=void, typename ...Args>
        T InvokeEvent(const std::string name, Args... args) {
            if (_luaEngine[name] == nullptr){
                return T();
            }

            try {
                sol::protected_function f(_luaEngine[name]);
                sol::protected_function_result res = f(args...);

                if (!res.valid()) {
                    sol::error err = res;
                    std::cerr << err.what() << std::endl;
                    return T();
                }

                if constexpr (std::is_same_v<T, void>) {
                    return;
                }
                else {
                    return res;
                }
            }
            catch (sol::error &ex) {
                std::cerr << ex.what() << std::endl;
                return T();
            }
        }
    };
} // namespace Framework::Scripting
