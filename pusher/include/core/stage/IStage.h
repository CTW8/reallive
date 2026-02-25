#pragma once

#include "core/Config.h"
#include "core/stage/Packets.h"

#include <atomic>
#include <string>
#include <vector>

namespace reallive::stage {

enum class StageState {
    kCreated = 0,
    kInited,
    kRunning,
    kStopping,
    kStopped,
    kFailed,
};

enum class PortDirection {
    kInput = 0,
    kOutput,
};

struct PortSpec {
    std::string name;
    PortDirection direction = PortDirection::kInput;
    PacketType packetType = PacketType::kUnknown;
};

struct StageInitContext {
    const PusherConfig* config = nullptr;
};

class IStage {
public:
    virtual ~IStage() = default;

    virtual std::string name() const = 0;
    virtual std::vector<PortSpec> ports() const = 0;

    virtual bool init(const StageInitContext& ctx) = 0;
    virtual bool start() = 0;
    virtual void requestStop() = 0;
    virtual void join() = 0;
    virtual StageState state() const = 0;
};

class StageBase : public IStage {
public:
    StageState state() const override {
        return state_.load();
    }

protected:
    void setState(StageState next) {
        state_.store(next);
    }

private:
    std::atomic<StageState> state_{StageState::kCreated};
};

} // namespace reallive::stage
