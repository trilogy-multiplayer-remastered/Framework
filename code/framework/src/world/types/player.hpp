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

#include <string>

namespace Framework::World::Archetypes {
    class PlayerFactory {
      private:
        inline void SetupDefaults(flecs::entity e, uint64_t guid) {
            auto &streamer = e.ensure<World::Modules::Base::Streamer>();
            streamer.guid      = guid;
        }

      public:
        inline void SetupClient(flecs::entity e, uint64_t guid) {
            SetupDefaults(e, guid);
        }

        inline void SetupServer(flecs::entity e, uint64_t guid, uint16_t playerIndex, const std::string &nickname) {
            SetupDefaults(e, guid);

            auto &streamable           = e.ensure<World::Modules::Base::Streamable>();
            streamable.assignOwnerProc = [](flecs::entity, World::Modules::Base::Streamable &) {
                return true; /* always keep current owner */
            };

            auto &streamer       = e.ensure<World::Modules::Base::Streamer>();
            streamer.nickname    = nickname;
            streamer.playerIndex = playerIndex;
            streamer.guid        = guid;
        }
    };
} // namespace Framework::World::Archetypes
