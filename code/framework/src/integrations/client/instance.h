/*
 * MafiaHub OSS license
 * Copyright (c) 2021, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "errors.h"

#include <external/discord/wrapper.h>
#include <graphics/renderer.h>
#include <memory>

namespace Framework::Integrations::Client {
    struct InstanceOptions {
        int64_t discordAppId = 0;
        bool usePresence     = true;
        bool useRenderer     = true;
        bool useNetworking   = true;

        Graphics::RendererConfiguration rendererOptions = {};
    };

    class Instance {
      private:
        bool _initialized = false;
        InstanceOptions _opts;

        std::unique_ptr<External::Discord::Wrapper> _presence;
        std::unique_ptr<Graphics::Renderer> _renderer;

      public:
        Instance();

        ClientError Init(InstanceOptions &);
        ClientError Shutdown();

        void Render();
        void Update();

        bool IsInitialized() const {
            return _initialized;
        }

        External::Discord::Wrapper *GetPresence() const {
            return _presence.get();
        }

        Graphics::Renderer *GetRenderer() const {
            return _renderer.get();
        }
    };
} // namespace Framework::Integrations::Client
