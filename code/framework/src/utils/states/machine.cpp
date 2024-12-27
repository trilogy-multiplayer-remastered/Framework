/*
 * MafiaHub OSS license
 * Copyright (c) 2021-2023, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#include "machine.h"
#include <logging/logger.h>

namespace Framework::Utils::States {
    Machine::Machine(): _currentState(nullptr), _nextState(nullptr), _currentContext(Context::Enter), _isUpdating(false) {}

    Machine::~Machine() {
        std::lock_guard<std::mutex> lock(_mutex);
        _states.clear();
    }

    bool Machine::validateStateTransition(int32_t stateId) const {
        return true;  // Override in derived classes for custom transition rules
    }

    bool Machine::RequestNextState(int32_t stateId) {
        std::lock_guard<std::mutex> lock(_mutex);
            
        if (!validateStateTransition(stateId)) {
            return false;
        }

        auto it = _states.find(stateId);
        if (it == _states.end()) {
            return false;
        }

        if (it->second.get() == _nextState) {
            return false;
        }

        if (_nextState != nullptr) {
            return false;
        }

        _nextState = it->second.get();
        _currentContext = Context::Exit;

        Framework::Logging::GetInstance()->Get(FRAMEWORK_INNER_UTILS)->debug("[StateMachine] Requesting new state {}", _nextState->GetName());
        return true;
    }

    bool Machine::Update() {
        std::lock_guard<std::mutex> lock(_mutex);
        
        if (_isUpdating) {
            Framework::Logging::GetInstance()->Get(FRAMEWORK_INNER_UTILS)->error("[StateMachine] Recursive update detected");
            return false;
        }
        
        _isUpdating = true;
        bool result = false;
        
        try {
            result = ProcessUpdate();
        }
        catch (const std::exception& e) {
            Framework::Logging::GetInstance()->Get(FRAMEWORK_INNER_UTILS)->error("[StateMachine] Error during update: {}", e.what());
            _isUpdating = false;
            throw;
        }
        
        _isUpdating = false;
        return result;
    }

    bool Machine::ProcessUpdate() {
        if (_currentState != nullptr) {
            switch (_currentContext) {
                case Context::Enter: {
                    try {
                        _currentContext = _currentState->OnEnter(this) ? Context::Update : Context::Exit;
                    }
                    catch (const std::exception& e) {
                        Framework::Logging::GetInstance()->Get(FRAMEWORK_INNER_UTILS)->error("[StateMachine] Error entering state {}: {}", _currentState->GetName(), e.what());
                        _currentContext = Context::Exit;
                        throw;
                    }
                    break;
                }

                case Context::Update: {
                    try {
                        if (_currentState->OnUpdate(this)) {
                            _currentContext = Context::Exit;
                        }
                    }
                    catch (const std::exception& e) {
                        Framework::Logging::GetInstance()->Get(FRAMEWORK_INNER_UTILS)->error("[StateMachine] Error updating state {}: {}", _currentState->GetName(), e.what());
                        _currentContext = Context::Exit;
                        throw;
                    }
                    break;
                }

                case Context::Exit: {
                    try {
                        _currentState->OnExit(this);
                    }
                    catch (const std::exception& e) {
                        Framework::Logging::GetInstance()->Get(FRAMEWORK_INNER_UTILS)->error("[StateMachine] Error exiting state {}: {}", _currentState->GetName(), e.what());
                        // Continue to next state despite error
                    }
                    _currentContext = Context::Next;
                    break;
                }

                case Context::Next: {
                    _currentState = _nextState;
                    _currentContext = Context::Enter;
                    _nextState = nullptr;
                    break;
                }

                default:
                    return false;
            }
        }
        else if (_nextState != nullptr) {
            _currentState = _nextState;
            _currentContext = Context::Enter;
            _nextState = nullptr;
        }
        else {
            return false;
        }

        return true;
    }
} // namespace Framework::Utils::States