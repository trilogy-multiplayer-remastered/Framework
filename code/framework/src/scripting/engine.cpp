#include "engine.h"

#include "builtins/color_rgb.h"
#include "builtins/color_rgba.h"
#include "builtins/quaternion.h"
#include "builtins/vector_2.h"
#include "builtins/vector_3.h"

namespace Framework::Scripting {
    int ConsoleLog(lua_State *L) {
        int nargs = lua_gettop(L);

        std::string str = "";

        for (int i = 1; i <= nargs; ++i) {
            str += luaL_tolstring(L, i, nullptr);
            lua_pop(L, 1); // remove the string
        }

        Framework::Logging::GetLogger(FRAMEWORK_INNER_SCRIPTING)->info(str);

        return 0;
    }

    /*static void On(const v8::FunctionCallbackInfo<v8::Value> &info) {
        // Ensure that the method was called with exactly two arguments
        if (info.Length() != 2) {
            v8::Isolate *isolate = info.GetIsolate();
            isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate, "Wrong number of arguments").ToLocalChecked()));
            return;
        }

        // Ensure that the first argument is a string and the second is a function
        if (!info[0]->IsString() || !info[1]->IsFunction()) {
            // Throw an error if the argument types are incorrect
            v8::Isolate *isolate = info.GetIsolate();
            isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate, "Invalid argument types: must be string and function").ToLocalChecked()));
            return;
        }

        const auto isolate = info.GetIsolate();
        const auto ctx     = isolate->GetEnteredOrMicrotaskContext();

        const v8::Local<v8::String> eventName       = info[0]->ToString(ctx).ToLocalChecked();
        const v8::Local<v8::Function> eventCallback = info[1].As<v8::Function>();

        // Create a persistent handle to the event callback function
        const v8::Persistent<v8::Function> persistentCallback(isolate, eventCallback);

        const auto engine = static_cast<Engine *>(ctx->GetAlignedPointerFromEmbedderData(0));
        engine->_eventHandlers[Helpers::ToString(eventName, isolate)].push_back(persistentCallback);
    }

    static void Emit(const v8::FunctionCallbackInfo<v8::Value> &info) {
        // Ensure that the method was called with exactly two arguments
        if (info.Length() != 2) {
            v8::Isolate *isolate = info.GetIsolate();
            isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate, "Wrong number of arguments").ToLocalChecked()));
            return;
        }

        // Ensure that both arguments are strings
        if (!info[0]->IsString() || !info[1]->IsString()) {
            // Throw an error if the argument types are incorrect
            v8::Isolate *isolate = info.GetIsolate();
            isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate, "Invalid argument types: must be string and string").ToLocalChecked()));
            return;
        }

        // TODO: check for reserved event names

        const auto isolate = info.GetIsolate();
        const auto ctx     = isolate->GetEnteredOrMicrotaskContext();

        const v8::Local<v8::String> eventName = info[0]->ToString(ctx).ToLocalChecked();
        const v8::Local<v8::String> eventData = info[1]->ToString(ctx).ToLocalChecked();

        const auto engine = static_cast<Engine *>(ctx->GetAlignedPointerFromEmbedderData(0));
        engine->InvokeEvent(Helpers::ToString(eventName, isolate), Helpers::ToString(eventData, isolate));
    }*/

    bool Engine::InitSDK(SDKRegisterCallback cb){
        auto luaState = _luaEngine.lua_state();

        _luaEngine["consoleLog"] = ConsoleLog;

        // Bind the builtins
        Builtins::ColorRGB::Register(_luaEngine);
        Builtins::ColorRGBA::Register(_luaEngine);
        Builtins::Quaternion::Register(_luaEngine);
        Builtins::Vector3::Register(_luaEngine);
        Builtins::Vector2::Register(_luaEngine);

        // Always bind the mod-side in last
        if (cb) {
            cb(Framework::Scripting::SDKRegisterWrapper(this));
        }

        return true;
    }
}