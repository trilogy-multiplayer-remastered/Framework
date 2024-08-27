/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"
#include "shared.h"

#include "engine.h"

namespace Framework::Scripting {
    class ClientEngine : public Engine {
        public:
            ClientEngine() = default;
            ~ClientEngine() = default;

            EngineError Init(SDKRegisterCallback) override;
            EngineError Shutdown() override;
            void Update() override;
    };
}
