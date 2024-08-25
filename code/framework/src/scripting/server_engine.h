/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <atomic>
#include <map>
#include <vector>

#include <node.h>

#include <cppfs/FileHandle.h>
#include <cppfs/FileWatcher.h>
#include <cppfs/fs.h>

#include <logging/logger.h>
#include <utils/time.h>

#include "engine.h"
#include "v8_helpers/v8_string.h"
#include "v8_helpers/v8_try_catch.h"

namespace Framework::Scripting {
    struct PackageDefinition {
        std::string path;
        std::string name;
        std::string version;
        std::string entrypoint;
    };

    class ServerEngine : public Engine {
      private:
        // Global
        cppfs::FileWatcher *_watcher;
        Utils::Time::TimePoint _nextFileWatchUpdate;
        int32_t _fileWatchUpdatePeriod = 1000;
        std::string _executionPath;

        // Global engine
        static inline bool _wasNodeAlreadyInited = false;
        v8::Persistent<v8::ObjectTemplate> _globalObjectTemplate;
        std::unique_ptr<node::MultiIsolatePlatform> _platform;
        v8::Persistent<v8::Context> _context;
        std::atomic<bool> _isShuttingDown = false;
        uv_loop_t uv_loop;

        // Package
        std::atomic<bool> _packageLoaded = false;
        PackageDefinition _packageDefinition = {};

        // Gamemode
        std::string _gamemodePath;
        v8::Persistent<v8::Script> _gamemodeScript;
        node::IsolateData *_gamemodeData        = nullptr;
        node::Environment *_gamemodeEnvironment = nullptr;
        bool _shouldReloadWatcher               = false;
        
      public:
        EngineError Init(SDKRegisterCallback) override;
        EngineError Shutdown() override;
        void Update() override;

        bool LoadPackageDefinitions();

        bool LoadScript();
        bool UnloadScript();

        bool CompileGamemodeScript(const std::string &str, const std::string &path);
        bool RunGamemodeScript() const;

        node::MultiIsolatePlatform *GetPlatform() const {
            return _platform.get();
        }

        v8::Local<v8::ObjectTemplate> GetGlobalObjectTemplate() const {
            return _globalObjectTemplate.Get(_isolate);
        }

        bool IsPackageLoaded() const {
            return _packageLoaded;
        }

        void SetExecutionPath(const std::string &path) {
            _executionPath = path;
        }
    };
} // namespace Framework::Scripting::Engines::Node
