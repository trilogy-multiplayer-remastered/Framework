/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <flecs/flecs.h>

#include "world/modules/base.hpp"

namespace Framework::World::Archetypes {
    class StreamingFactory {
      private:
        inline void SetupDefaults(flecs::entity e, uint64_t guid) {
            e.add<Framework::World::Modules::Base::Transform>();

            auto &streamable                 = e.ensure<Framework::World::Modules::Base::Streamable>();
            streamable.owner                 = guid;
            streamable.defaultUpdateInterval = CoreModules::GetTickRate() * 1000.0f; // we need ms here

            e.add<Framework::World::Modules::Base::TickRateRegulator>();
        }

      public:
        inline void SetupClient(flecs::entity e, uint64_t guid) {
            SetupDefaults(e, guid);

            auto& streamable = e.ensure<Framework::World::Modules::Base::Streamable>();
            Framework::World::Modules::Base::SetupClientEmitters(streamable);

            auto ass = e.get_mut<Framework::World::Modules::Base::Streamable>();
            (void)ass;
        }

        inline void SetupServer(flecs::entity e, uint64_t guid) {
            SetupDefaults(e, guid);

            auto& streamable = e.ensure<Framework::World::Modules::Base::Streamable>();
            Framework::World::Modules::Base::SetupServerEmitters(streamable);
        }
    };
} // namespace Framework::World::Archetypes
