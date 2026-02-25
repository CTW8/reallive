#include <gtest/gtest.h>

#include "core/stage/IStage.h"
#include "core/stage/StageGraph.h"

#include <memory>
#include <string>
#include <vector>

using namespace reallive::stage;

namespace {

class DummyStage : public StageBase {
public:
    DummyStage(std::string stageName, std::vector<PortSpec> p)
        : stageName_(std::move(stageName)), ports_(std::move(p)) {}

    std::string name() const override { return stageName_; }
    std::vector<PortSpec> ports() const override { return ports_; }
    bool init(const StageInitContext&) override { setState(StageState::kInited); return true; }
    bool start() override { setState(StageState::kRunning); return true; }
    void requestStop() override { setState(StageState::kStopping); }
    void join() override { setState(StageState::kStopped); }

private:
    std::string stageName_;
    std::vector<PortSpec> ports_;
};

} // namespace

TEST(StageGraphTest, ConnectValidPorts) {
    StageGraph graph;

    auto a = std::make_shared<DummyStage>(
        "a",
        std::vector<PortSpec>{{"out", PortDirection::kOutput, PacketType::kFrame}}
    );
    auto b = std::make_shared<DummyStage>(
        "b",
        std::vector<PortSpec>{{"in", PortDirection::kInput, PacketType::kFrame}}
    );

    const NodeId na = graph.addStage(a);
    const NodeId nb = graph.addStage(b);
    ASSERT_NE(na, 0u);
    ASSERT_NE(nb, 0u);

    EXPECT_TRUE(graph.connect(na, "out", nb, "in", EdgeOptions{}));
    EXPECT_TRUE(graph.validate());
}

TEST(StageGraphTest, RejectMismatchedPacketType) {
    StageGraph graph;

    auto a = std::make_shared<DummyStage>(
        "a",
        std::vector<PortSpec>{{"out", PortDirection::kOutput, PacketType::kFrame}}
    );
    auto b = std::make_shared<DummyStage>(
        "b",
        std::vector<PortSpec>{{"in", PortDirection::kInput, PacketType::kAudio}}
    );

    const NodeId na = graph.addStage(a);
    const NodeId nb = graph.addStage(b);
    ASSERT_NE(na, 0u);
    ASSERT_NE(nb, 0u);

    EXPECT_FALSE(graph.connect(na, "out", nb, "in", EdgeOptions{}));
}

TEST(StageGraphTest, DetectCycle) {
    StageGraph graph;

    auto a = std::make_shared<DummyStage>(
        "a",
        std::vector<PortSpec>{
            {"in", PortDirection::kInput, PacketType::kFrame},
            {"out", PortDirection::kOutput, PacketType::kFrame},
        }
    );
    auto b = std::make_shared<DummyStage>(
        "b",
        std::vector<PortSpec>{
            {"in", PortDirection::kInput, PacketType::kFrame},
            {"out", PortDirection::kOutput, PacketType::kFrame},
        }
    );

    const NodeId na = graph.addStage(a);
    const NodeId nb = graph.addStage(b);
    ASSERT_NE(na, 0u);
    ASSERT_NE(nb, 0u);

    EXPECT_TRUE(graph.connect(na, "out", nb, "in", EdgeOptions{}));
    EXPECT_FALSE(graph.connect(nb, "out", na, "in", EdgeOptions{}));
}
