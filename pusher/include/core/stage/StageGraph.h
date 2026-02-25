#pragma once

#include "core/stage/EdgeQueue.h"
#include "core/stage/IStage.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace reallive::stage {

using NodeId = uint32_t;
using EdgeId = uint32_t;

struct EdgeOptions {
    size_t capacity = 4;
    QueuePolicy policy = QueuePolicy::kDropOldest;
};

struct GraphNodeInfo {
    NodeId id = 0;
    std::string name;
};

struct GraphEdgeInfo {
    EdgeId id = 0;
    NodeId from = 0;
    std::string outPort;
    NodeId to = 0;
    std::string inPort;
    EdgeOptions options;
};

class StageGraph {
public:
    StageGraph() = default;

    NodeId addStage(const std::shared_ptr<IStage>& stage);
    bool connect(
        NodeId from,
        const std::string& outPort,
        NodeId to,
        const std::string& inPort,
        const EdgeOptions& options,
        EdgeId* outEdgeId = nullptr
    );

    bool validate() const;
    bool hasCycle() const;
    std::vector<NodeId> topologicalOrder() const;

    bool initAll(const StageInitContext& ctx);
    bool startAll();
    void requestStopAll();
    void joinAll();

    std::vector<GraphNodeInfo> nodes() const;
    std::vector<GraphEdgeInfo> edges() const;
    std::shared_ptr<void> edgeQueue(EdgeId id) const;

private:
    struct NodeEntry {
        NodeId id = 0;
        std::shared_ptr<IStage> stage;
        std::unordered_map<std::string, PortSpec> inputs;
        std::unordered_map<std::string, PortSpec> outputs;
    };

    bool hasNode(NodeId id) const;
    bool buildPortIndex(NodeEntry& entry) const;
    bool portTypeMatch(const PortSpec& outPort, const PortSpec& inPort) const;
    bool canConnect(NodeId from, const std::string& outPort, NodeId to, const std::string& inPort) const;
    std::shared_ptr<void> createQueueForPacketType(PacketType type, const EdgeOptions& options) const;
    void bindStageOutput(const NodeEntry& node, const std::string& outPort, const std::shared_ptr<void>& queue);

    NodeId nextNodeId_ = 1;
    EdgeId nextEdgeId_ = 1;
    std::unordered_map<NodeId, NodeEntry> nodes_;
    std::unordered_map<EdgeId, GraphEdgeInfo> edges_;
    std::unordered_map<EdgeId, std::shared_ptr<void>> edgeQueues_;
};

} // namespace reallive::stage
