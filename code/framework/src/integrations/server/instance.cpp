/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "instance.h"

#include "world/server.h"

#include "networking/messages/client_connection_finalized.h"
#include "networking/messages/client_handshake.h"
#include "networking/messages/client_initialise_player.h"
#include "networking/messages/client_kick.h"
#include "networking/messages/messages.h"

#include "../shared/modules/mod.hpp"

#include "utils/version.h"

#include "cxxopts.hpp"

#include "scripting/builtins/entity.h"

#include <cppfs/FileHandle.h>
#include <cppfs/fs.h>
#include <csignal>

#include "core_modules.h"

namespace Framework::Integrations::Server {
    Instance::Instance(): _alive(false), _shuttingDown(false) {
        _networkingEngine = std::make_shared<Networking::Engine>();
        _webServer        = std::make_shared<HTTP::Webserver>();
        _fileConfig       = std::make_unique<Utils::Config>();
        _worldEngine      = std::make_shared<World::ServerEngine>();
        _scriptingEngine  = std::make_shared<Scripting::ServerEngine>(_worldEngine);
        _playerFactory    = std::make_shared<World::Archetypes::PlayerFactory>();
        _streamingFactory = std::make_shared<World::Archetypes::StreamingFactory>();
        _masterlist       = std::make_unique<Services::MasterlistConnector>();
    }

    Instance::~Instance() {
        sig_detach(this);
    }

    ServerError Instance::Init(InstanceOptions &opts) {
        _opts = opts;

        if (opts.gameName.empty() || opts.gameVersion.empty()) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Game name and version are required");
            return ServerError::SERVER_INVALID_OPTIONS;
        }

        CoreModules::SetTickRate(opts.tickInterval);

        // First level is argument parser, because we might want to overwrite stuffs
        cxxopts::Options options(_opts.modSlug, _opts.modHelpText);
        options.allow_unrecognised_options();
        options.add_options("MafiaHub Integrations server",
            {{"p,port", "Networking port to bind", cxxopts::value<int32_t>()->default_value(std::to_string(_opts.bindPort))}, {"h,host", "Networking host to bind", cxxopts::value<std::string>()->default_value(_opts.bindHost)},
                {"c,config", "JSON config file to read", cxxopts::value<std::string>()->default_value(_opts.modConfigFile)}, {"P,apiport", "HTTP API port to bind", cxxopts::value<int32_t>()->default_value(std::to_string(_opts.webBindPort))},
                {"H,apihost", "HTTP API host to bind", cxxopts::value<std::string>()->default_value(_opts.webBindHost)}, {"help", "Prints this help message", cxxopts::value<bool>()->default_value("false")}});

        // Try to parse and return if anything wrong happened
        const auto result = options.parse(_opts.argc, _opts.argv);

        // If help was specified, just print the help and exit
        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            exit(0);
        }

        // Allow mod to specify custom JSON config file name
        _opts.modConfigFile = result["config"].as<std::string>();

        // Load JSON config if present
        if (!LoadConfigFromJSON()) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("Failed to parse JSON config file");
            return ServerError::SERVER_CONFIG_PARSE_ERROR;
        }

        // Finally apply back to the structure that is used everywhere the settings from the parser
        _opts.bindHost = result["host"].as<std::string>();
        _opts.bindPort = result["port"].as<int32_t>();

        // Initialize the logging instance with the mod slug name
        Logging::GetInstance()->SetLogName("server");

        // Initialize the web server
        if (!_webServer->Init(_opts.webBindHost, _opts.webBindPort, _opts.httpServeDir)) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("Failed to initialize the webserver engine");
            return ServerError::SERVER_WEBSERVER_INIT_FAILED;
        }

        // Initialize our networking engine
        if (!_networkingEngine->Init(_opts.bindPort, _opts.bindHost, _opts.maxPlayers, _opts.bindPassword)) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("Failed to initialize the networking engine");
            return ServerError::SERVER_NETWORKING_INIT_FAILED;
        }

        // Initialize the world
        if (_worldEngine->Init(_networkingEngine->GetNetworkServer(), _opts.streamerTickInterval) != World::EngineError::ENGINE_NONE) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("Failed to initialize the world engine");
            return ServerError::SERVER_WORLD_INIT_FAILED;
        }

        if (_opts.bindPublicServer && !_masterlist->Init(_opts.bindSecretKey)) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to contact masterlist server: Push key is empty");
        }
        else if (!_opts.bindPublicServer) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->warn("Server will not be announced to masterlist");
        }
        else {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Masterlist connector initialized");
        }

        // Init the signals handlers if enabled
        if (_opts.enableSignals) {
            sig_attach(SIGINT, sig_slot(this, &Instance::OnSignal), sig_ctx_sys());
            sig_attach(SIGTERM, sig_slot(this, &Instance::OnSignal), sig_ctx_sys());
        }

        // Register the default endpoints
        InitEndpoints();

        // Register built in modules
        InitModules();

        // Initialize default messages
        InitNetworkingMessages();

        // Initialize mod subsystems
        PostInit();
    
        const auto sdkCallback = [this](Framework::Scripting::SDKRegisterWrapper<Framework::Scripting::ServerEngine> sdk) {
            this->RegisterScriptingBuiltins(sdk.GetEngine());
        };

        // Initialize the scripting engine
        _scriptingEngine->SetMainPath("gamemode");
        _scriptingEngine->LoadManifest();
        if (_scriptingEngine->InitServerEngine(sdkCallback) != Framework::Scripting::ModuleError::MODULE_NONE) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("Failed to initialize the scripting engine");
            return ServerError::SERVER_SCRIPTING_INIT_FAILED;
        }

        // Load the gamemode
        _scriptingEngine->GetServerEngine()->LoadScript();

        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Host:\t{}", _opts.bindHost);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Port:\t{}", _opts.bindPort);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Max Players:\t{}", _opts.maxPlayers);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("{} Server successfully started", _opts.modName);
        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->flush();

        _alive        = true;
        _shuttingDown = false;
        return ServerError::SERVER_NONE;
    }

    void Instance::InitEndpoints() {
        _webServer->RegisterRequest("/", [this](const httplib::Request &req, httplib::Response &res) {
            nlohmann::json root;
            root["mod_name"]          = _opts.modName;
            root["mod_slug"]          = _opts.modSlug;
            root["mod_version"]       = Utils::Version::rel;
            root["host"]              = _opts.bindHost;
            root["port"]              = _opts.bindPort;
            root["password_required"] = !_opts.bindPassword.empty();
            root["max_players"]       = _opts.maxPlayers;
            res.body                  = root.dump(4);
            res.status                = 200;
        });

        Logging::GetLogger(FRAMEWORK_INNER_HTTP)->debug("All core endpoints have been registered!");
    }

    void Instance::InitModules() const {
        if (_worldEngine) {
            const auto world = _worldEngine->GetWorld();

            world->import <Integrations::Shared::Modules::Mod>();
        }

        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Core ecs modules have been imported!");
    }

    bool Instance::LoadConfigFromJSON() {
        const auto configHandle = cppfs::fs::open(_opts.modConfigFile);

        if (!configHandle.exists()) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("JSON config file is not present, skipping load...");
            return true;
        }

        const auto configData = configHandle.readFile();

        try {
            // Parse our config data first
            _fileConfig->Parse(configData);

            if (!_fileConfig->IsParsed()) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("JSON config load has failed: {}", _fileConfig->GetLastError());
                return false;
            }

            // Retrieve fields and overwrite InstanceOptions defaults
            _opts.bindHost      = _fileConfig->Get<std::string>("host");
            _opts.bindPort      = _fileConfig->Get<int>("port");
            _opts.bindMapName   = _fileConfig->Get<std::string>("map");
            _opts.maxPlayers    = _fileConfig->Get<int>("maxplayers");
            _opts.bindSecretKey = _fileConfig->Get<std::string>("server-token");
        }
        catch (const std::exception &ex) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->critical("JSON config has missing fields: {}", ex.what());
            return false;
        }
        return true;
    }

    void Instance::InitNetworkingMessages() const {
        using namespace Framework::Networking::Messages;
        const auto net = _networkingEngine->GetNetworkServer();
        net->RegisterMessage<ClientHandshake>(Framework::Networking::Messages::GameMessages::GAME_CONNECTION_HANDSHAKE, [this, net](SLNet::RakNetGUID guid, ClientHandshake *msg) {
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Received handshake message for player {}", msg->GetPlayerName());

            // Make sure handshake payload was correctly formatted
            if (!msg->Valid()) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Handshake payload was invalid, force-disconnecting peer");
                net->GetPeer()->CloseConnection(guid, true);
                return;
            }

            if (msg->GetGameName() != _opts.gameName) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Client has invalid game, force-disconnecting peer");
                Framework::Networking::Messages::ClientKick kick;
                kick.FromParameters(Framework::Networking::Messages::DisconnectionReason::WRONG_VERSION);
                net->Send(kick, guid);
                net->GetPeer()->CloseConnection(guid, true);
                return;
            }

            const auto fwVersion = msg->GetFWVersion();

            if (!Utils::Version::VersionSatisfies(fwVersion.c_str(), Utils::Version::rel)) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Client has invalid Framework version, force-disconnecting peer");
                Framework::Networking::Messages::ClientKick kick;
                kick.FromParameters(Framework::Networking::Messages::DisconnectionReason::WRONG_VERSION);
                net->Send(kick, guid);
                net->GetPeer()->CloseConnection(guid, true);
                return;
            }

            const auto clientVersion = msg->GetClientVersion();

            if (!Utils::Version::VersionSatisfies(clientVersion.c_str(), _opts.modVersion.c_str())) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Client has invalid version, force-disconnecting peer");
                Framework::Networking::Messages::ClientKick kick;
                kick.FromParameters(Framework::Networking::Messages::DisconnectionReason::WRONG_VERSION);
                net->Send(kick, guid);
                net->GetPeer()->CloseConnection(guid, true);
                return;
            }

            const auto mpClientVersion = msg->GetGameVersion();

            if (mpClientVersion != _opts.gameVersion) {
                Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Client has invalid game version, force-disconnecting peer");
                Framework::Networking::Messages::ClientKick kick;
                kick.FromParameters(Framework::Networking::Messages::DisconnectionReason::WRONG_VERSION);
                net->Send(kick, guid);
                net->GetPeer()->CloseConnection(guid, true);
                return;
            }

            // Create player entity and add on world
            const auto newPlayer = _worldEngine->CreateEntity();
            _streamingFactory->SetupServer(newPlayer, guid.g);

            auto nickname = msg->GetPlayerName();
            if (nickname.size() > 64) {
                nickname = nickname.substr(0, 64);
            }

            _playerFactory->SetupServer(newPlayer, guid.g, guid.systemIndex, nickname);

            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->info("Player {} guid {} entity id {}", msg->GetPlayerName(), guid.g, newPlayer.id());

            // Send the connection finalized packet
            Framework::Networking::Messages::ClientConnectionFinalized answer;
            answer.FromParameters(_opts.tickInterval, newPlayer.id());
            net->Send(answer, guid);
        });

        net->SetOnPlayerDisconnectCallback([this, net](SLNet::Packet *packet, uint32_t reason) {
            const auto guid = packet->guid;
            Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Disconnecting peer {}, reason: {}", guid.g, reason);

            const auto e = _worldEngine->GetEntityByGUID(guid.g);
            _worldEngine->GetWorld()->defer_begin();
            if (e.is_valid()) {
                if (_onPlayerDisconnectCallback)
                    _onPlayerDisconnectCallback(e, guid.g);

                _worldEngine->RemoveEntity(e);
            }
            _worldEngine->GetWorld()->defer_end();

            net->GetPeer()->CloseConnection(guid, true);
        });

        net->RegisterMessage<ClientInitPlayer>(Framework::Networking::Messages::GameMessages::GAME_INIT_PLAYER, [this, net](SLNet::RakNetGUID guid, ClientInitPlayer *stub) {
            const auto e = _worldEngine->GetEntityByGUID(guid.g);
            if (_onPlayerConnectCallback && e.is_valid() && e.is_alive())
                _onPlayerConnectCallback(e, guid.g);
        });

        Framework::World::Modules::Base::SetupServerReceivers(net, _worldEngine.get());

        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Game sync networking messages registered");
    }

    ServerError Instance::Shutdown() {
        if (_shuttingDown) {
            return ServerError::SERVER_NONE;
        }

        _shuttingDown = true;

        if (_networkingEngine) {
            _networkingEngine->Shutdown();
        }

        if (_scriptingEngine) {
            _scriptingEngine->Shutdown();
        }

        if (_webServer) {
            _webServer->Shutdown();
        }

        if (_worldEngine) {
            _worldEngine->Shutdown();
        }

        // Detach signal handlers
        sig_detach(SIGINT, sig_slot(this, &Instance::OnSignal));
        sig_detach(SIGTERM, sig_slot(this, &Instance::OnSignal));

        CoreModules::Reset();

        _alive = false;
        return ServerError::SERVER_NONE;
    }

    void Instance::Update() {
        const auto start = std::chrono::high_resolution_clock::now();
        if (_nextTick <= start) {
            if (_networkingEngine) {
                _networkingEngine->Update();
            }

            if (_scriptingEngine) {
                _scriptingEngine->Update();
            }

            if (_worldEngine) {
                _worldEngine->Update();
            }

            if (_masterlist->IsInitialized()) {
                Services::ServerInfo info {};
                info.gameMode       = _scriptingEngine->GetServerEngine()->GetScriptName();
                info.version        = Utils::Version::rel;
                info.maxPlayers     = _opts.maxPlayers;
                info.currentPlayers = _networkingEngine->GetNetworkServer()->GetPeer()->NumberOfConnections();
                info.webPort        = _opts.webBindPort;
                info.gamePort       = _opts.bindPort;
                _masterlist->Ping(info);
            }

            PostUpdate();

            _nextTick = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(static_cast<int64_t>(_opts.tickInterval * 1000.0f));
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    void Instance::Run() {
        while (_alive) {
            Update();
            std::this_thread::yield();
        }
    }

    void Instance::OnSignal(const sig_signal_t signal) {
        if (!_alive || _shuttingDown) {
            return;
        }

        if (signal.context != sig_ctx_sys()) {
            return;
        }

        Logging::GetLogger(FRAMEWORK_INNER_SERVER)->debug("Received shutdown signal. In progress...");

        PreShutdown();
        Shutdown();
    }

    void Instance::RegisterScriptingBuiltins(Framework::Scripting::ServerEngine *engine) {
        // Register the entity builtin
        Framework::Integrations::Scripting::Entity::Register(engine->GetLuaEngine());

        // mod-specific builtins
        ModuleRegister(engine);
    }
} // namespace Framework::Integrations::Server
