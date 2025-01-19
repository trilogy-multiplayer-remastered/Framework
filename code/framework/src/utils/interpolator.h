#pragma once

#include "time.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <glm/ext.hpp>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <stdexcept>

namespace Framework::Utils {
    namespace math {
        inline float Unlerp(const float from, const float to, const float pos) {
            if (from == to) return 1.0f;
            return (pos - from) / (to - from);
        }

        inline float Unlerp(const Time::TimePoint &from, const Time::TimePoint &to, const Time::TimePoint &pos) {
            const float range = std::chrono::duration<float, std::milli>(to - from).count();
            if (range < std::numeric_limits<float>::epsilon()) return 1.0f;
            return std::chrono::duration<float, std::milli>(pos - from).count() / range;
        }
    } // namespace math

    class Interpolator {
      public:
        using TimePoint = std::chrono::high_resolution_clock::time_point;

        template <typename T>
        class Value {
          public:
            virtual ~Value() = default;
            Value() : _startTime(TimePoint::max()), _finishTime(TimePoint::max()) {}

            virtual bool HasTargetValue() const {
                return _finishTime != TimePoint::max();
            }

            virtual void SetTargetValue(const T &current, const T &target, float delay) = 0;
            virtual T UpdateTargetValue(const T &current) = 0;

            void SetErrorContributionDelayRange(float delayMin, float delayMax) {
                _delayMin = delayMin;
                _delayMax = delayMax;
            }

            void SetCompensationFactor(float factor) {
                _compensationFactor = factor;
            }

            void SetDebugTime(int64_t debugTime) {
                _debugEnabled = true;
                _debugTime = std::chrono::milliseconds(debugTime);
            }

          protected:
            T _start{};
            T _end{};
            T _error{};
            TimePoint _startTime;
            TimePoint _finishTime;
            float _lastAlpha = 0.0f;
            float _delayMin = 100.0f;
            float _delayMax = 400.0f;
            float _compensationFactor = 1.0f;
            bool _debugEnabled = false;
            std::chrono::milliseconds _debugTime{};

            TimePoint GetCurrentTime() const {
                if (_debugEnabled) {
                    return _startTime + _debugTime;
                }
                return std::chrono::high_resolution_clock::now();
            }
        };

        Interpolator() = default;

        template <typename T>
        void RegisterProperty(const std::string &name) {
            static_assert(std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::quat> || std::is_same_v<T, float>,
                          "Only glm::vec3, glm::quat, and float are supported types.");
            _properties.emplace(name, std::make_unique<ConcreteValue<T>>());
        }

        template <typename T>
        Value<T> *GetProperty(const std::string &name) {
            auto it = _properties.find(name);
            if (it != _properties.end()) {
                return dynamic_cast<Value<T> *>(it->second.get());
            }
            throw std::runtime_error("Property not found or type mismatch.");
        }

      private:
        class ConcreteBase {
          public:
            virtual ~ConcreteBase() = default;
        };

        template <typename T>
        class ConcreteValue final : public Value<T>, public ConcreteBase {
          public:
            void SetTargetValue(const T &current, const T &target, float delay) override {
                this->_start = current;
                this->_end = target;
                this->_error = target - current;
                this->_startTime = this->GetCurrentTime();
                this->_finishTime = this->_startTime + std::chrono::milliseconds(static_cast<int>(delay));
                this->_lastAlpha = 0.0f;
            }

            T UpdateTargetValue(const T &current) override {
                if (!this->HasTargetValue()) return current;

                float alpha = math::Unlerp(this->_startTime, this->_finishTime, this->GetCurrentTime());
                alpha = std::clamp(alpha, 0.0f, 1.0f); // Ensure alpha is clamped between 0 and 1
                return glm::mix(current, this->_end, alpha);
            }
        };

        // Using std::unordered_map with unique_ptr to store property data safely
        std::unordered_map<std::string, std::unique_ptr<ConcreteBase>> _properties;

        // Deleted copy constructor and assignment operator to prevent issues with unique_ptr
        Interpolator(const Interpolator &) = delete;
        Interpolator &operator=(const Interpolator &) = delete;

        // Allow move semantics
        Interpolator(Interpolator &&) noexcept = default;
        Interpolator &operator=(Interpolator &&) noexcept = default;
    };
} // namespace Framework::Utils
