#include "core/stage/StageGraph.h"
#include "core/stage/CaptureStage.h"
#include "core/stage/DetectStage.h"
#include "core/stage/EncodeStage.h"
#include "core/stage/AudioStage.h"
#include "core/stage/OutputStage.h"
#include "core/stage/ProcessStage.h"

#include <algorithm>
#include <deque>
#include <iostream>
#include <unordered_set>

namespace reallive::stage {

NodeId StageGraph::addStage(const std::shared_ptr<IStage>& stage) {
    if (!stage) return 0;

    NodeEntry entry;
    entry.id = nextNodeId_++;
    entry.stage = stage;

    if (!buildPortIndex(entry)) {
        return 0;
    }

    nodes_[entry.id] = std::move(entry);
    return entry.id;
}

bool StageGraph::connect(
    NodeId from,
    const std::string& outPort,
    NodeId to,
    const std::string& inPort,
    const EdgeOptions& options,
    EdgeId* outEdgeId
) {
    if (!canConnect(from, outPort, to, inPort)) {
        return false;
    }

    GraphEdgeInfo edge;
    edge.id = nextEdgeId_++;
    edge.from = from;
    edge.outPort = outPort;
    edge.to = to;
    edge.inPort = inPort;
    edge.options = options;
    edges_[edge.id] = edge;
    edgeQueues_[edge.id] = createQueueForPacketType(
        nodes_.at(from).outputs.at(outPort).packetType,
        options
    );

    if (hasCycle()) {
        edges_.erase(edge.id);
        edgeQueues_.erase(edge.id);
        return false;
    }

    bindStageOutput(nodes_.at(from), outPort, edgeQueues_[edge.id]);
    const auto& toNode = nodes_.at(to);
    if (toNode.stage && edgeQueues_[edge.id]) {
        if (inPort == "raw_frame") {
            auto detect = std::dynamic_pointer_cast<DetectStage>(toNode.stage);
            if (detect) {
                auto q = std::static_pointer_cast<EdgeQueue<FramePacket>>(edgeQueues_[edge.id]);
                detect->setInputQueue(q);
            }
            auto process = std::dynamic_pointer_cast<ProcessStage>(toNode.stage);
            if (process) {
                auto q = std::static_pointer_cast<EdgeQueue<FramePacket>>(edgeQueues_[edge.id]);
                process->setFrameInputQueue(q);
            }
        } else if (inPort == "detection_meta") {
            auto process = std::dynamic_pointer_cast<ProcessStage>(toNode.stage);
            if (process) {
                auto q = std::static_pointer_cast<EdgeQueue<DetectionPacket>>(edgeQueues_[edge.id]);
                process->setDetectionInputQueue(q);
            }
        } else if (inPort == "processed_frame") {
            auto encode = std::dynamic_pointer_cast<EncodeStage>(toNode.stage);
            if (encode) {
                auto q = std::static_pointer_cast<EdgeQueue<ProcessedFramePacket>>(edgeQueues_[edge.id]);
                encode->setInputQueue(q);
            }
        } else if (inPort == "encoded_video") {
            auto output = std::dynamic_pointer_cast<OutputStage>(toNode.stage);
            if (output) {
                auto q = std::static_pointer_cast<EdgeQueue<EncodedPacketEx>>(edgeQueues_[edge.id]);
                output->setInputQueue(q);
            }
        } else if (inPort == "audio_frame") {
            auto output = std::dynamic_pointer_cast<OutputStage>(toNode.stage);
            if (output) {
                auto q = std::static_pointer_cast<EdgeQueue<AudioPacket>>(edgeQueues_[edge.id]);
                output->setAudioInputQueue(q);
            }
        }
    }

    if (outEdgeId) {
        *outEdgeId = edge.id;
    }
    return true;
}

bool StageGraph::validate() const {
    return !hasCycle();
}

bool StageGraph::hasCycle() const {
    return topologicalOrder().size() != nodes_.size();
}

std::vector<NodeId> StageGraph::topologicalOrder() const {
    std::unordered_map<NodeId, size_t> indegree;
    std::unordered_map<NodeId, std::vector<NodeId>> adjacency;
    indegree.reserve(nodes_.size());
    adjacency.reserve(nodes_.size());

    for (const auto& kv : nodes_) {
        indegree[kv.first] = 0;
        adjacency[kv.first] = {};
    }

    for (const auto& kv : edges_) {
        const GraphEdgeInfo& edge = kv.second;
        if (!hasNode(edge.from) || !hasNode(edge.to)) {
            continue;
        }
        adjacency[edge.from].push_back(edge.to);
        indegree[edge.to] += 1;
    }

    std::deque<NodeId> q;
    for (const auto& kv : indegree) {
        if (kv.second == 0) {
            q.push_back(kv.first);
        }
    }

    std::vector<NodeId> order;
    order.reserve(nodes_.size());
    while (!q.empty()) {
        const NodeId id = q.front();
        q.pop_front();
        order.push_back(id);

        auto adjIt = adjacency.find(id);
        if (adjIt == adjacency.end()) continue;
        for (NodeId next : adjIt->second) {
            auto inIt = indegree.find(next);
            if (inIt == indegree.end()) continue;
            if (inIt->second > 0) {
                inIt->second -= 1;
                if (inIt->second == 0) {
                    q.push_back(next);
                }
            }
        }
    }

    return order;
}

bool StageGraph::initAll(const StageInitContext& ctx) {
    const auto order = topologicalOrder();
    if (order.size() != nodes_.size()) {
        std::cerr << "[StageGraph] initAll failed: graph has cycle" << std::endl;
        return false;
    }

    for (NodeId id : order) {
        auto it = nodes_.find(id);
        if (it == nodes_.end() || !it->second.stage) {
            std::cerr << "[StageGraph] initAll failed: node missing" << std::endl;
            return false;
        }
        if (!it->second.stage->init(ctx)) {
            std::cerr << "[StageGraph] init stage failed: " << it->second.stage->name() << std::endl;
            return false;
        }
    }
    return true;
}

bool StageGraph::startAll() {
    const auto order = topologicalOrder();
    if (order.size() != nodes_.size()) {
        std::cerr << "[StageGraph] startAll failed: graph has cycle" << std::endl;
        return false;
    }

    for (NodeId id : order) {
        auto it = nodes_.find(id);
        if (it == nodes_.end() || !it->second.stage) {
            std::cerr << "[StageGraph] startAll failed: node missing" << std::endl;
            return false;
        }
        if (!it->second.stage->start()) {
            std::cerr << "[StageGraph] start stage failed: " << it->second.stage->name() << std::endl;
            return false;
        }
    }
    return true;
}

void StageGraph::requestStopAll() {
    auto order = topologicalOrder();
    if (order.size() != nodes_.size()) {
        order.clear();
        order.reserve(nodes_.size());
        for (const auto& kv : nodes_) {
            order.push_back(kv.first);
        }
    }
    std::reverse(order.begin(), order.end());

    for (NodeId id : order) {
        auto it = nodes_.find(id);
        if (it == nodes_.end() || !it->second.stage) continue;
        it->second.stage->requestStop();
    }
}

void StageGraph::joinAll() {
    auto order = topologicalOrder();
    if (order.size() != nodes_.size()) {
        order.clear();
        order.reserve(nodes_.size());
        for (const auto& kv : nodes_) {
            order.push_back(kv.first);
        }
    }
    std::reverse(order.begin(), order.end());

    for (NodeId id : order) {
        auto it = nodes_.find(id);
        if (it == nodes_.end() || !it->second.stage) continue;
        it->second.stage->join();
    }
}

std::vector<GraphNodeInfo> StageGraph::nodes() const {
    std::vector<GraphNodeInfo> out;
    out.reserve(nodes_.size());
    for (const auto& kv : nodes_) {
        GraphNodeInfo info;
        info.id = kv.first;
        info.name = kv.second.stage ? kv.second.stage->name() : "unknown";
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(), [](const GraphNodeInfo& a, const GraphNodeInfo& b) {
        return a.id < b.id;
    });
    return out;
}

std::vector<GraphEdgeInfo> StageGraph::edges() const {
    std::vector<GraphEdgeInfo> out;
    out.reserve(edges_.size());
    for (const auto& kv : edges_) {
        out.push_back(kv.second);
    }
    std::sort(out.begin(), out.end(), [](const GraphEdgeInfo& a, const GraphEdgeInfo& b) {
        return a.id < b.id;
    });
    return out;
}

std::shared_ptr<void> StageGraph::edgeQueue(EdgeId id) const {
    auto it = edgeQueues_.find(id);
    if (it == edgeQueues_.end()) {
        return nullptr;
    }
    return it->second;
}

bool StageGraph::hasNode(NodeId id) const {
    return nodes_.find(id) != nodes_.end();
}

bool StageGraph::buildPortIndex(NodeEntry& entry) const {
    if (!entry.stage) return false;

    const auto specs = entry.stage->ports();
    std::unordered_set<std::string> names;
    names.reserve(specs.size());

    for (const auto& spec : specs) {
        if (spec.name.empty()) {
            std::cerr << "[StageGraph] stage port name is empty: " << entry.stage->name() << std::endl;
            return false;
        }
        if (!names.insert(spec.name).second) {
            std::cerr << "[StageGraph] duplicate port: " << entry.stage->name()
                      << ":" << spec.name << std::endl;
            return false;
        }
        if (spec.packetType == PacketType::kUnknown) {
            std::cerr << "[StageGraph] unknown packet type on port: " << entry.stage->name()
                      << ":" << spec.name << std::endl;
            return false;
        }

        if (spec.direction == PortDirection::kInput) {
            entry.inputs[spec.name] = spec;
        } else {
            entry.outputs[spec.name] = spec;
        }
    }

    return true;
}

bool StageGraph::portTypeMatch(const PortSpec& outPort, const PortSpec& inPort) const {
    return outPort.packetType == inPort.packetType;
}

bool StageGraph::canConnect(NodeId from, const std::string& outPort, NodeId to, const std::string& inPort) const {
    const auto fromIt = nodes_.find(from);
    const auto toIt = nodes_.find(to);
    if (fromIt == nodes_.end() || toIt == nodes_.end()) {
        return false;
    }
    if (from == to) {
        return false;
    }

    const auto outIt = fromIt->second.outputs.find(outPort);
    const auto inIt = toIt->second.inputs.find(inPort);
    if (outIt == fromIt->second.outputs.end() || inIt == toIt->second.inputs.end()) {
        return false;
    }

    if (!portTypeMatch(outIt->second, inIt->second)) {
        return false;
    }

    for (const auto& kv : edges_) {
        const auto& edge = kv.second;
        if (edge.from == from && edge.outPort == outPort &&
            edge.to == to && edge.inPort == inPort) {
            return false;
        }
    }

    return true;
}

std::shared_ptr<void> StageGraph::createQueueForPacketType(PacketType type, const EdgeOptions& options) const {
    switch (type) {
    case PacketType::kFrame:
        return std::make_shared<EdgeQueue<FramePacket>>(options.capacity, options.policy);
    case PacketType::kDetection:
        return std::make_shared<EdgeQueue<DetectionPacket>>(options.capacity, options.policy);
    case PacketType::kProcessedFrame:
        return std::make_shared<EdgeQueue<ProcessedFramePacket>>(options.capacity, options.policy);
    case PacketType::kEncodedVideo:
        return std::make_shared<EdgeQueue<EncodedPacketEx>>(options.capacity, options.policy);
    case PacketType::kAudio:
        return std::make_shared<EdgeQueue<AudioPacket>>(options.capacity, options.policy);
    default:
        return nullptr;
    }
}

void StageGraph::bindStageOutput(const NodeEntry& node, const std::string& outPort, const std::shared_ptr<void>& queue) {
    if (!node.stage || !queue) return;

    if (outPort == "raw_frame") {
        auto capture = std::dynamic_pointer_cast<CaptureStage>(node.stage);
        if (capture) {
            auto frameQueue = std::static_pointer_cast<EdgeQueue<FramePacket>>(queue);
            capture->setOutputQueue(frameQueue);
        }
        return;
    }

    if (outPort == "detection_meta") {
        auto detect = std::dynamic_pointer_cast<DetectStage>(node.stage);
        if (detect) {
            auto detectQueue = std::static_pointer_cast<EdgeQueue<DetectionPacket>>(queue);
            detect->setOutputQueue(detectQueue);
        }
        return;
    }

    if (outPort == "processed_frame") {
        auto process = std::dynamic_pointer_cast<ProcessStage>(node.stage);
        if (process) {
            auto processQueue = std::static_pointer_cast<EdgeQueue<ProcessedFramePacket>>(queue);
            process->setOutputQueue(processQueue);
        }
        return;
    }

    if (outPort == "encoded_video") {
        auto encode = std::dynamic_pointer_cast<EncodeStage>(node.stage);
        if (encode) {
            auto encodeQueue = std::static_pointer_cast<EdgeQueue<EncodedPacketEx>>(queue);
            encode->setOutputQueue(encodeQueue);
        }
        return;
    }

    if (outPort == "audio_frame") {
        auto audio = std::dynamic_pointer_cast<AudioStage>(node.stage);
        if (audio) {
            auto audioQueue = std::static_pointer_cast<EdgeQueue<AudioPacket>>(queue);
            audio->setOutputQueue(audioQueue);
        }
    }
}

} // namespace reallive::stage
