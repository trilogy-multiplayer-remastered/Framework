/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include <nlohmann/json.hpp>
#include <uv.h>

#include "server_engine.h"
#include "events.h"
#include "core_modules.h"
#include "world/server.h"

static constexpr char bootstrap_code[] = R"(
const publicRequire = require("module").createRequire(process.cwd() + "/gamemode/server/");
globalThis.require = publicRequire;
require("vm").runInThisContext(process.argv[1]);
)";

namespace Framework::Scripting {
    EngineError ServerEngine::Init(SDKRegisterCallback cb) {
        if (_wasNodeAlreadyInited) {
            return EngineError::ENGINE_NODE_INIT_FAILED;
        }
        // Define the arguments to be passed to the node instance
        std::vector<std::string> args = {"mafiahub-server", "--experimental-modules", "--experimental-specifier-resolution=node", "--no-warnings"};
        std::vector<std::string> exec_args {};
        std::vector<std::string> errors {};

        // Initialize the node with the provided arguments
        const int initCode = node::InitializeNodeWithArgs(&args, &exec_args, &errors);
        if (initCode != 0) {
            for (std::string &error : errors) {
                Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Failed to initialize node: {}", error);
            }
            return Framework::Scripting::EngineError::ENGINE_NODE_INIT_FAILED;
        }

        // Initialize the platform
        _platform = node::MultiIsolatePlatform::Create(4);
        v8::V8::InitializePlatform(GetPlatform());
        v8::V8::Initialize();

        uv_loop_init(&uv_loop);

        // Allocate and register the isolate
        _isolate = node::NewIsolate(node::CreateArrayBufferAllocator(), &uv_loop, _platform.get());

        // Allocate our scopes
        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);
        v8::HandleScope handleScope(_isolate);

        // Create our global object template
        const v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(_isolate);

        // Initialize our SDK and bind to the global object template
        InitSDK(cb);
        global->Set(v8pp::to_v8(_isolate, "sdk"), GetObjectTemplate());

        // Reset and save the global object template pointer
        _globalObjectTemplate.Reset(_isolate, global);

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Node.JS engine initialized!");
        _isShuttingDown       = false;
        _wasNodeAlreadyInited = true;
        return EngineError::ENGINE_NONE;
    }

    EngineError ServerEngine::Shutdown() {
        if (!_wasNodeAlreadyInited) {
            return EngineError::ENGINE_NODE_SHUTDOWN_FAILED;
        }

        if (!_platform) {
            return EngineError::ENGINE_PLATFORM_NULL;
        }

        if (!_isolate) {
            return EngineError::ENGINE_ISOLATE_NULL;
        }

        _isShuttingDown = true;

        // Release the isolate
        bool platform_finished = false;
        _platform->AddIsolateFinishedCallback(
            _isolate,
            [](void *data) {
                *static_cast<bool *>(data) = true;
            },
            &platform_finished);
        _platform->UnregisterIsolate(_isolate);

        // Wait until the platform has cleaned up all relevant resources.
        {
            v8::Locker locker(_isolate);
            v8::Isolate::Scope isolate_scope(_isolate);
            while (!platform_finished) uv_run(&uv_loop, UV_RUN_ONCE);
            uv_loop_close(&uv_loop);
        }

        _isolate->Dispose();
        _isolate = nullptr;

        // Release node and v8 engines
        v8::V8::Dispose();
        v8::V8::DisposePlatform();
        node::FreePlatform(_platform.release());

        return EngineError::ENGINE_NONE;
    }

    void ServerEngine::Update() {
        if (!_platform) {
            return;
        }

        if (!_isolate) {
            return;
        }

        if (_isShuttingDown) {
            return;
        }

        // Update the base scripting layer
        {
            v8::Locker locker(_isolate);
            v8::Isolate::Scope isolateScope(_isolate);
            v8::SealHandleScope seal(_isolate);
            _platform->DrainTasks(_isolate);

            // Notify the package, if loaded
            if (_packageLoaded) {
                InvokeEvent(Events[EventIDs::GAMEMODE_UPDATED]);
            }
        }

        // Update the file watcher
        /*if (_watcher && !_isShuttingDown && Utils::Time::Compare(_nextFileWatchUpdate, Utils::Time::GetTimePoint()) < 0) {
            // Process the file changes watcher
            _watcher->watch(0);
            _nextFileWatchUpdate = Utils::Time::Add(Utils::Time::GetTimePoint(), _fileWatchUpdatePeriod);

            if (_shouldReloadWatcher) {
                _shouldReloadWatcher = false;
                delete _watcher;
                PreloadGamemode(_gamemodePath);
            }
        }*/
    }

    bool ServerEngine::LoadPackageDefinitions(){
        // Check the package.json file exists
        const cppfs::FileHandle packageFile = cppfs::fs::open(_executionPath + "/package.json");
        if (!packageFile.exists() || !packageFile.isFile()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("The gamemode package.json handle does not exists or is not a file");
            return false;
        }

        // Load the package.json file
        std::string packageFileContent = packageFile.readFile();
        if (packageFileContent.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("The gamemode package.json is empty");
            return false;
        }

        auto root = nlohmann::json::parse(packageFileContent);
        try {
            _packageDefinition.name       = root["name"].get<std::string>();
            _packageDefinition.version    = root["version"].get<std::string>();
            _packageDefinition.entrypoint = root["main"].get<std::string>();
            return true;
        }
        catch (nlohmann::detail::type_error &err) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->error("The gamemode package.json is not valid:\n\t{}", err.what());
            return false;
        }
        return true;
    }

    bool ServerEngine::LoadScript(){
        // Package cannot be loaded twice
        if(_packageLoaded){
            return false;
        }

        // Make sure to reload the definitions
        if(!LoadPackageDefinitions()){
            return false;
        }

        // Make sure the specified entrypoint is valid
        const cppfs::FileHandle entryPointFile = cppfs::fs::open(_executionPath + "/" + _packageDefinition.entrypoint);
        if (!entryPointFile.exists() || !entryPointFile.isFile()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("The specified entrypoint '{}' from '{}' is not a file", _packageDefinition.entrypoint, _packageDefinition.name);
            return false;
        }

        // Read the entrypoint file content
        const std::string content = entryPointFile.readFile();
        if (content.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("The specified entrypoint file '{}' from '{}' is empty", _packageDefinition.entrypoint, _packageDefinition.name);
            return false;
        }

        // Allocate our scopes
        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolateScope(_isolate);
        v8::HandleScope handleScope(_isolate);

        // Create our gamemode context
        const v8::Local<v8::Context> context = v8::Context::New(_isolate, nullptr, _globalObjectTemplate.Get(_isolate));
        v8::Context::Scope scope(context);

        // Assign ourselves to the context
        context->SetAlignedPointerInEmbedderData(0, this);
        _context.Reset(_isolate, context);

        // Create the execution environment
        constexpr auto flags                = node::EnvironmentFlags::kOwnsProcessState;
        _gamemodeData                       = node::CreateIsolateData(_isolate, &uv_loop, GetPlatform());
        const std::vector<std::string> argv = {"mafiahub-gamemode"};
        _gamemodeEnvironment                = node::CreateEnvironment(_gamemodeData, context, argv, argv, flags);

        // Make sure isolate is linked to our node environment
        constexpr node::IsolateSettings is;
        node::SetIsolateUpForNode(_isolate, is);

        // Load the environment
        node::LoadEnvironment(_gamemodeEnvironment, bootstrap_code);

        // Compile the gamemode
        CompileGamemodeScript(content, entryPointFile.path());
        RunGamemodeScript();

        // Invoke the gamemode loaded event
        InvokeEvent(Events[EventIDs::GAMEMODE_LOADED]);

        _packageLoaded = true;
        return true;
    }

    bool ServerEngine::UnloadScript(){
        if(!_packageLoaded){
            return false;
        }

        // Invoke the gamemode unloading event
        InvokeEvent(Events[EventIDs::GAMEMODE_UNLOADING]);

        // Purge all gamemode entities
        // TODO: this has nothing to do with the scripting engine, and shouldn't be here
        /*const auto worldEngine = CoreModules::GetWorldEngine();
        if (worldEngine) {
            worldEngine->PurgeAllGameModeEntities();
        }*/

        // Stop node environment
        node::Stop(_gamemodeEnvironment);

        // Scope the gamemode
        v8::Locker locker(_isolate);
        v8::Isolate::Scope isolate_scope(_isolate);
        {
            v8::SealHandleScope seal(_isolate);

            bool more = false;
            // Drain the remaining tasks while there are available ones
            do {
                uv_run(&uv_loop, UV_RUN_DEFAULT);
                _platform->DrainTasks(_isolate);

                more = uv_loop_alive(&uv_loop);
                if (more)
                    continue;

                if (node::EmitProcessBeforeExit(_gamemodeEnvironment).IsNothing())
                    break;

                more = uv_loop_alive(&uv_loop);
            } while (more);

            // Exit node environment
            node::EmitProcessExit(_gamemodeEnvironment);
        }

        // Release memory
        node::FreeIsolateData(_gamemodeData);
        node::FreeEnvironment(_gamemodeEnvironment);

        _eventHandlers.clear();
        _packageLoaded = false;
        return true;
        
        return true;
    }

    bool ServerEngine::CompileGamemodeScript(const std::string &str, const std::string &path) {
        v8::Isolate::Scope isolateScope(_isolate);
        v8::HandleScope handleScope(_isolate);

        Helpers::TryCatch([&] {
            const auto context = _context.Get(_isolate);
            v8::Context::Scope contextScope(context);

            const auto source = Helpers::MakeString(_isolate, str).ToLocalChecked();
            v8::MaybeLocal<v8::Script> script;
            if (path.empty()) {
                script = v8::Script::Compile(context, source);
            }
            else {
                const auto originValue = Helpers::MakeString(_isolate, path).ToLocalChecked();
                v8::ScriptOrigin scriptOrigin(_isolate, originValue);

                script = v8::Script::Compile(context, source, &scriptOrigin);
            }
            if (script.IsEmpty())
                return false;

            _gamemodeScript.Reset(_isolate, script.ToLocalChecked());
            return true;
        });
        return true;
    }

    bool ServerEngine::RunGamemodeScript() const {
        if (_gamemodeScript.IsEmpty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Invalid gamemode script object");
            return false;
        }

        v8::Isolate::Scope isolateScope(_isolate);
        v8::HandleScope handleScope(_isolate);
        Helpers::TryCatch([&] {
            const auto context = _context.Get(_isolate);
            v8::Context::Scope contextScope(context);

            _gamemodeScript.Get(_isolate)->Run(context);
            return true;
        });
        return true;
    }

    /*
    bool ServerEngine::WatchGamemodeChanges(std::string path) {
        cppfs::FileHandle dir = cppfs::fs::open(path);
        if (!dir.isDirectory()) {
            return false;
        }

        _watcher = new cppfs::FileWatcher;

        Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Watching '{}' changes", dir.path().c_str());
        _watcher->add(dir, cppfs::FileCreated | cppfs::FileRemoved | cppfs::FileModified | cppfs::FileAttrChanged, cppfs::Recursive);
        _watcher->addHandler([this, path](cppfs::FileHandle &fh, cppfs::FileEvent ev) {
            if (_shouldReloadWatcher)
                return;

            Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->debug("Gamemode is reloaded due to the file changes");
            // Close the gamemode first, we'll start with a clean slate
            if (this->IsGamemodeLoaded() && UnloadGamemode(path)) {
                _shouldReloadWatcher = true;
            }
        });

        _nextFileWatchUpdate = Utils::Time::Add(Utils::Time::GetTimePoint(), _fileWatchUpdatePeriod);
        return true;
    }*/
} // namespace Framework::Scripting::Engines::Node
