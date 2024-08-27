/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include <cppfs/FileHandle.h>
#include <cppfs/FileIterator.h>
#include <cppfs/fs.h>
#include <logging/logger.h>
#include <nlohmann/json.hpp>
#include <regex>

#include "module.h"

#include "core_modules.h"

namespace Framework::Scripting {
    ModuleError Module::InitClientEngine(SDKRegisterCallback cb) {
        // TODO: implement
        return ModuleError::MODULE_NONE;
    }

    ModuleError Module::InitServerEngine(SDKRegisterCallback cb) {
        // Validate the presence of the gamemode and the gamemode server sub folder
        const cppfs::FileHandle serverFolder = cppfs::fs::open(_mainPath + "/server");
        if (!serverFolder.exists() || serverFolder.isFile()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug(fmt::format("The server folder '{}' does not exist", _mainPath + "/server"));
            return ModuleError::MODULE_MISSING_GAMEMODE;
        }

        // Init should return an error if the engine failed to initialize
        _serverEngine = std::make_unique<ServerEngine>();
        _serverEngine->SetExecutionPath(_mainPath + "/server");
        if (_serverEngine->Init(cb) != EngineError::ENGINE_NONE) {
            _serverEngine.reset();
            return ModuleError::MODULE_ENGINE_INIT_FAILED;
        }

        // Make sure we have at least one server file to load
        if(_serverFiles.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("No server files to load");
            return ModuleError::MODULE_ENGINE_INIT_FAILED;
        }

        // For now, always load the first in the list
        const std::string serverFile = _serverFiles[0];
        _serverEngine->SetScriptName(serverFile);
        if (!_serverEngine->LoadScript()) {
            return ModuleError::MODULE_ENGINE_INIT_FAILED;
        }

        CoreModules::SetScriptingModule(this);
        return ModuleError::MODULE_NONE;
    }

    ModuleError Module::LoadManifest() {
        // Check the manifest.json file exists
        const cppfs::FileHandle manifestFile = cppfs::fs::open(_mainPath + "/manifest.json");
        if (!manifestFile.exists() || !manifestFile.isFile()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("The gamemode manifest.json handle does not exists or is not a file");
            return ModuleError::MODULE_RESOURCE_MANAGER_NULL;
        }

        // Load the manifest.json file
        std::string manifestJsonContent = manifestFile.readFile();
        if (manifestJsonContent.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("The gamemode manifest.json is empty");
            return ModuleError::MODULE_RESOURCE_MANAGER_NULL;
        }

        auto root = nlohmann::json::parse(manifestJsonContent);
        try {
            _clientFiles = root["client_files"].get<std::vector<std::string>>();
            _serverFiles = root["server_files"].get<std::vector<std::string>>();
        }
        catch (nlohmann::detail::type_error &err) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("The gamemode package.json is not valid:\n\t{}", err.what());
            return ModuleError::MODULE_RESOURCE_MANAGER_NULL;
        }
        return ModuleError::MODULE_NONE;
    }

    ModuleError Module::Shutdown() {
        if (_clientEngine.get() != nullptr) {
            _clientEngine->Shutdown();
            _clientEngine.reset();
        }

        if (_serverEngine.get() != nullptr) {
            _serverEngine->Shutdown();
            _serverEngine.reset();
        }

        CoreModules::SetScriptingModule(nullptr);
        return ModuleError::MODULE_NONE;
    }

    void Module::Update() const {
        if (_clientEngine.get() != nullptr) {
            _clientEngine->Update();
        }

        if (_serverEngine.get() != nullptr) {
            _serverEngine->Update();
        }
    }
} // namespace Framework::Scripting
