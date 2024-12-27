// state.h
#pragma once

#include <cstdint>
#include <string_view>

namespace Framework::Utils::States {
    class Machine;
    
    class IState {
    public:
        virtual ~IState() = default;

        virtual std::string_view GetName() const = 0;
        virtual int32_t GetId() const = 0;

        virtual bool OnEnter(Machine* machine) = 0;
        virtual bool OnUpdate(Machine* machine) = 0;
        virtual bool OnExit(Machine* machine) = 0;

    protected:
        IState() = default;
        
        // Prevent slicing
        IState(const IState&) = delete;
        IState& operator=(const IState&) = delete;
    };
}