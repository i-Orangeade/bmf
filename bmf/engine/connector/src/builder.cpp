/*
 * Copyright 2023 Babit Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <utility>

#include "../include/builder.hpp"
#include "../../c_engine/include/module_factory.h"
#include "../../c_engine/include/graph_config.h"
#include "../../c_engine/include/optimizer.h"
#include "../../c_engine/include/common.h"
#include "../include/connector.hpp"

namespace bmf::builder {
namespace internal {
RealStream::RealStream(const std::shared_ptr<RealNode> &node, std::string name,
                       std::string notify, std::string alias)
    : node_(node), graph_(node->graph_), name_(std::move(name)),
      notify_(std::move(notify)), alias_(std::move(alias)) {}

RealStream::RealStream(const std::shared_ptr<RealGraph> &graph,
                       std::string name, std::string notify, std::string alias)
    : graph_(graph), name_(std::move(name)), notify_(std::move(notify)),
      alias_(std::move(alias)) {
    std::shared_ptr<RealNode> nil;
    node_ = nil;
}

void RealStream::SetNotify(std::string const &notify) {
    auto node = node_.lock();
    if (!node)
        throw std::logic_error("Could not call SetNotify on an input stream.");
    int idx = -1;
    for (idx = 0; idx < node->outputStreams_.size(); ++idx)
        if (node->outputStreams_[idx]->name_ == name_)
            break;
    if (idx < 0)
        throw std::logic_error("Internal error.");
    node->GiveStreamNotify(idx, notify);
}

void RealStream::SetAlias(std::string const &alias) {
    auto node = node_.lock();
    if (!node)
        throw std::logic_error("Could not call SetAlias on an input stream.");
    int idx = -1;
    for (idx = 0; idx < node->outputStreams_.size(); ++idx)
        if (node->outputStreams_[idx]->name_ == name_)
            break;
    if (idx < 0)
        throw std::logic_error("Internal error.");
    node->GiveStreamAlias(idx, alias);
}

std::shared_ptr<RealNode> RealStream::AddModule(
    std::string const &alias, const bmf_sdk::JsonParam &option,
    std::vector<std::shared_ptr<RealStream>> inputStreams,
    std::string const &moduleName, ModuleType moduleType,
    std::string const &modulePath, std::string const &moduleEntry,
    InputManagerType inputStreamManager, int scheduler) {
    inputStreams.insert(inputStreams.begin(), shared_from_this());
    return graph_.lock()->AddModule(alias, option, inputStreams, moduleName,
                                    moduleType, modulePath, moduleEntry,
                                    inputStreamManager, scheduler);
}

nlohmann::json RealStream::Dump() {
    nlohmann::json info;

    info["identifier"] = (notify_.empty() ? "" : (notify_ + ":")) + name_;
    info["alias"] = alias_;

    return info;
}

void RealStream::Start() {
    std::vector<std::shared_ptr<internal::RealStream>> generateRealStreams;
    generateRealStreams.emplace_back(shared_from_this());
    graph_.lock()->Start(generateRealStreams, false, true);
}

std::string RealStream::GetName() { return name_; }

RealNode::ModuleMetaInfo::ModuleMetaInfo(std::string moduleName,
                                         ModuleType moduleType,
                                         std::string modulePath,
                                         std::string moduleEntry)
    : moduleName_(std::move(moduleName)), moduleType_(moduleType),
      modulePath_(std::move(modulePath)), moduleEntry_(std::move(moduleEntry)) {
}

nlohmann::json RealNode::ModuleMetaInfo::Dump() {
    nlohmann::json info;

    switch (moduleType_) {
    case C:
        info["type"] = "c";
        break;
    case CPP:
        info["type"] = "c++";
        break;
    case Python:
        info["type"] = "python";
        break;
    case Go:
        info["type"] = "go";
        break;
    }
    info["name"] = moduleName_;
    info["path"] = modulePath_;
    info["entry"] = moduleEntry_;

    return info;
}

nlohmann::json RealNode::NodeMetaInfo::Dump() {
    nlohmann::json info;

    info["premodule_id"] = preModuleUID_;
    info["callback_binding"] = 
        nlohmann::json(std::vector<std::string>());
    for (auto &kv : callbackBinding_) {
        info["callback_binding"].push_back(
            std::to_string(kv.first) + ":" + std::to_string(kv.second)
        );
    }

    return info;
}

RealNode::RealNode(const std::shared_ptr<RealGraph> &graph, int id,
                   std::string alias, const bmf_sdk::JsonParam &option,
                   std::vector<std::shared_ptr<RealStream>> inputStreams,
                   std::string const &moduleName, ModuleType moduleType,
                   std::string const &modulePath,
                   std::string const &moduleEntry,
                   InputManagerType inputStreamManager, int scheduler)
    : graph_(graph), id_(id), alias_(std::move(alias)), option_(option),
      moduleInfo_({moduleName, moduleType, modulePath, moduleEntry}),
      metaInfo_(), inputStreams_(std::move(inputStreams)),
      inputManager_(inputStreamManager), scheduler_(scheduler) {
    //            outputStreams_.reserve(BMF_MAX_CAPACITY);
}

std::shared_ptr<RealStream> RealNode::Stream(int idx) {
    if (idx < 0)
        throw std::overflow_error("Requesting unexisted stream using index.");
    //            if (idx >= BMF_MAX_CAPACITY)
    //                throw std::overflow_error("Stream index bigger than max
    //                capacity (1024 by default).");
    for (auto i = outputStreams_.size(); i <= idx; ++i) {
        auto buf = new char[255];
        std::sprintf(buf, "%s_%d_%lu", moduleInfo_.moduleName_.c_str(), id_, i);
        outputStreams_.emplace_back(std::move(std::make_shared<RealStream>(
            shared_from_this(), std::string(buf), "", "")));
        delete[] buf;
    }
    return outputStreams_[idx];
}

std::shared_ptr<RealStream> RealNode::Stream(std::string const &name) {
    auto graph = graph_.lock();
    if (graph->existedStreamAlias_.count(name) &&
        graph->existedStreamAlias_[name]->node_.lock().get() == this)
        return graph->existedStreamAlias_[name];
    if (existedStreamNotify_.count(name))
        return existedStreamNotify_[name];

    throw std::logic_error(
        "Requesting unexisted stream using name. (Not an alias nor notify.)");
}

void RealNode::SetAlias(std::string const &alias) {
    graph_.lock()->GiveNodeAlias(shared_from_this(), alias);
}

void RealNode::GiveStreamNotify(int idx, std::string const &notify) {
    auto graph = graph_.lock();
    if (graph->existedNodeAlias_.count(notify))
        throw std::logic_error(
            "Duplicated stream notify with existing node alias.");
    if (graph->existedStreamAlias_.count(notify))
        throw std::logic_error(
            "Duplicated stream notify with existing stream alias.");
    if (existedStreamNotify_.count(notify))
        throw std::logic_error(
            "Duplicated stream notify with existing stream notify.");
    existedStreamNotify_[notify] = outputStreams_[idx];
    outputStreams_[idx]->notify_ = notify;
}

void RealNode::GiveStreamAlias(int idx, std::string const &alias) {
    graph_.lock()->GiveStreamAlias(outputStreams_[idx], alias);
}

void RealNode::SetInputManager(InputManagerType inputStreamManager) {
    if (graph_.lock()->mode_ == ServerMode) {
        if (inputStreamManager != Server)
            throw std::logic_error("cannot set input_manager other than Server "
                                   "to node in graph set to ServerMode");
    }
    inputManager_ = inputStreamManager;
}

void RealNode::SetScheduler(int scheduler) { scheduler_ = scheduler; }

void RealNode::SetPreModule(bmf::BMFModule preModuleInstance) {
    metaInfo_.preModuleInstance_ =
        std::make_shared<bmf::BMFModule>(preModuleInstance);
    metaInfo_.preModuleUID_ = preModuleInstance.uid();
}

void RealNode::AddCallback(long long key, bmf::BMFCallback callbackInstance) {
    metaInfo_.callbackInstances_[key] =
        std::make_shared<bmf::BMFCallback>(callbackInstance);
    metaInfo_.callbackBinding_[key] = callbackInstance.uid();
}
void RealNode::AddCallback(long long key, std::function<bmf_sdk::CBytes(bmf_sdk::CBytes)> callback) {
    auto cb = bmf::BMFCallback(callback);
    metaInfo_.callbackInstances_[key] =
        std::make_shared<bmf::BMFCallback>(cb);
    metaInfo_.callbackBinding_[key] = cb.uid();
}

std::shared_ptr<RealNode>
RealNode::AddModule(std::string const &alias, const bmf_sdk::JsonParam &option,
                    std::vector<std::shared_ptr<RealStream>> inputStreams,
                    std::string const &moduleName, ModuleType moduleType,
                    std::string const &modulePath,
                    std::string const &moduleEntry,
                    InputManagerType inputStreamManager, int scheduler) {
    inputStreams.insert(inputStreams.begin(), Stream(0));
    return graph_.lock()->AddModule(alias, option, inputStreams, moduleName,
                                    moduleType, modulePath, moduleEntry,
                                    inputStreamManager, scheduler);
}

nlohmann::json RealNode::Dump() {
    nlohmann::json info;

    info["id"] = id_;
    info["alias"] = alias_;
    info["module_info"] = moduleInfo_.Dump();
    info["meta_info"] = metaInfo_.Dump();
    info["input_streams"] = nlohmann::json::array();
    for (auto &s : inputStreams_)
        info["input_streams"].push_back(s->Dump());
    info["output_streams"] = nlohmann::json::array();
    for (auto &s : outputStreams_)
        info["output_streams"].push_back(s->Dump());
    info["option"] = option_.json_value_;
    info["scheduler"] = scheduler_;
    switch (inputManager_) {
    case Default:
        info["input_manager"] = "default";
        break;
    case Immediate:
        info["input_manager"] = "immediate";
        break;
    case Server:
        info["input_manager"] = "server";
        break;
    case FrameSync:
        info["input_manager"] = "framesync";
        break;
    case ClockSync:
        info["input_manager"] = "clocksync";
        break;
    default:
        info["input_manager"] = "default";
    }

    return info;
}

RealGraph::RealGraph(GraphMode runMode, const bmf_sdk::JsonParam &graphOption)
    : mode_(runMode), graphOption_(graphOption), placeholderNode_(nullptr) {}

void RealGraph::GiveStreamAlias(std::shared_ptr<RealStream> stream,
                                std::string const &alias) {
    if (existedNodeAlias_.count(alias))
        throw std::logic_error(
            "Duplicated stream alias with existing node alias.");
    if (existedStreamAlias_.count(alias))
        throw std::logic_error(
            "Duplicated stream alias with existing stream alias.");
    for (auto &nd : nodes_)
        if (nd->existedStreamNotify_.count(alias))
            throw std::logic_error(
                "Duplicated stream alias with existing stream notify.");
    existedStreamAlias_[alias] = std::move(stream);
    existedStreamAlias_[alias]->alias_ = alias;
}

void RealGraph::GiveNodeAlias(std::shared_ptr<RealNode> node,
                              std::string const &alias) {
    if (existedNodeAlias_.count(alias))
        throw std::logic_error(
            "Duplicated node alias with existing node alias.");
    if (existedStreamAlias_.count(alias))
        throw std::logic_error(
            "Duplicated node alias with existing stream alias.");
    for (auto &nd : nodes_)
        if (nd->existedStreamNotify_.count(alias))
            throw std::logic_error(
                "Duplicated node alias with existing stream notify.");
    existedNodeAlias_[alias] = std::move(node);
    existedNodeAlias_[alias]->alias_ = alias;
}

std::shared_ptr<RealNode> RealGraph::AddModule(
    std::string const &alias, const bmf_sdk::JsonParam &option,
    const std::vector<std::shared_ptr<RealStream>> &inputStreams,
    std::string const &moduleName, ModuleType moduleType,
    std::string const &modulePath, std::string const &moduleEntry,
    InputManagerType inputStreamManager, int scheduler) {
    //            if (nodes_.size() + 1 >= BMF_MAX_CAPACITY)
    //                throw std::overflow_error("Node number bigger than max
    //                capacity (1024 by default).");
    if (mode_ == ServerMode)
        inputStreamManager = Server;
    int node_id = nodes_.size();
    nodes_.emplace_back(std::move(std::make_shared<RealNode>(
        shared_from_this(), node_id, alias, option, inputStreams, moduleName,
        moduleType, modulePath, moduleEntry, inputStreamManager, scheduler)));
    return nodes_[node_id];
}

std::shared_ptr<RealNode> RealGraph::GetAliasedNode(std::string const &alias) {
    if (!existedNodeAlias_.count(alias))
        throw std::logic_error("Unexisted aliased node.");
    return existedNodeAlias_[alias];
}

std::shared_ptr<RealStream>
RealGraph::GetAliasedStream(std::string const &alias) {
    if (!existedStreamAlias_.count(alias))
        throw std::logic_error("Unexisted aliased stream.");
    return existedStreamAlias_[alias];
}

std::shared_ptr<RealStream> RealGraph::NewPlaceholderStream() {
    if (placeholderNode_ == nullptr)
        placeholderNode_ = std::move(std::make_shared<RealNode>(
            shared_from_this(), std::numeric_limits<int>::max(), "",
            bmf_sdk::JsonParam(), std::vector<std::shared_ptr<RealStream>>(),
            "BMFPlaceholderNode", CPP, "", "", Immediate, 0));

    return placeholderNode_->Stream(placeholderNode_->outputStreams_.size());
}

nlohmann::json RealGraph::Dump() {
    nlohmann::json info;

    info["input_streams"] = nlohmann::json::array();
    info["output_streams"] = nlohmann::json::array();
    info["nodes"] = nlohmann::json::array();
    info["option"] = graphOption_.json_value_;
    switch (mode_) {
    case NormalMode:
        info["mode"] = "Normal";
        break;
    case ServerMode:
        info["mode"] = "Server";
        break;
    case GeneratorMode:
        info["mode"] = "Generator";
        break;
    case SubGraphMode:
        info["mode"] = "Subgraph";
        break;
    case PushDataMode:
        info["mode"] = "Pushdata";
        break;
    }
    for (auto &nd : nodes_)
        info["nodes"].push_back(nd->Dump());
    for (auto &s : inputStreams_)
        info["input_streams"].push_back(s->Dump());
    for (auto &s : outputStreams_)
        info["output_streams"].push_back(s->Dump());

    return info;
}

void RealGraph::SetOption(const bmf_sdk::JsonParam &optionPatch) {
    graphOption_.merge_patch(optionPatch);
}

void RealGraph::Start(bool dumpGraph, bool needMerge) {
    auto graph_config = Dump().dump(4);
    BMFLOG(BMF_INFO) << graph_config << std::endl;
    if (dumpGraph || (graphOption_.json_value_.count("dump_graph") &&
                      graphOption_.json_value_["dump_graph"] == 1)) {
        BMFLOG(BMF_INFO) << "graph_config dump" << std::endl;
        std::ofstream graph_file("graph.json", std::ios::app);
        BMFLOG(BMF_INFO) << graph_config << std::endl;
        graph_file << graph_config;
        graph_file.close();
    }
    if (graphInstance_ == nullptr)
        graphInstance_ =
            std::make_shared<bmf::BMFGraph>(graph_config, false, needMerge);
    graphInstance_->start();
}

int RealGraph::Run(bool dumpGraph, bool needMerge) {
    auto graph_config = Dump().dump(4);
    if (dumpGraph || (graphOption_.json_value_.count("dump_graph") &&
                      graphOption_.json_value_["dump_graph"] == 1)) {
        std::ofstream graph_file("graph.json", std::ios::app);
        graph_file << graph_config;
        graph_file.close();
    }
    if (graphInstance_ == nullptr)
        graphInstance_ =
            std::make_shared<bmf::BMFGraph>(graph_config, false, needMerge);
    graphInstance_->start();
    return graphInstance_->close();
}

void RealGraph::Start(
    const std::vector<std::shared_ptr<internal::RealStream>> &streams,
    bool dumpGraph, bool needMerge) {
    outputStreams_.insert(outputStreams_.end(), streams.begin(), streams.end());
    Start(dumpGraph, needMerge);
}

int RealGraph::Close() {
    return graphInstance_->close();
}

int RealGraph::ForceClose() {
    return graphInstance_->force_close();
}

bmf::BMFGraph RealGraph::Instantiate(bool dumpGraph, bool needMerge) {
    auto graph_config = Dump().dump(4);
    if (dumpGraph || (graphOption_.json_value_.count("dump_graph") &&
                      graphOption_.json_value_["dump_graph"] == 1)) {
        std::ofstream graph_file("graph.json", std::ios::app);
        graph_file << graph_config;
        graph_file.close();
    }
    if (graphInstance_ == nullptr)
        graphInstance_ =
            std::make_shared<bmf::BMFGraph>(graph_config, false, needMerge);
    return *graphInstance_;
}

bmf::BMFGraph RealGraph::Instance() {
    if (graphInstance_ == nullptr)
        throw std::logic_error(
            "trying to get graph instance before instantiated.");
    return *graphInstance_;
}

Packet RealGraph::Generate(std::string streamName, bool block) {
    return graphInstance_->poll_output_stream_packet(streamName, block);
}

int RealGraph::FillPacket(std::string streamName, Packet packet, bool block) {
    return graphInstance_->add_input_stream_packet(streamName, packet, block);
}

std::shared_ptr<RealStream> RealGraph::InputStream(std::string streamName,
                                                   std::string notify,
                                                   std::string alias) {
    auto realStream = std::make_shared<internal::RealStream>(
        shared_from_this(), streamName, notify, alias);
    inputStreams_.emplace_back(realStream);
    return realStream;
}

} // namespace internal

std::string GetVersion() { return BMF_VERSION; }

std::string GetCommit() { return BMF_COMMIT; }

void ChangeDmpPath(std::string path) { bmf::ChangeDmpPath(path); }

bmf::BMFModule GetModuleInstance(std::string const &moduleName,
                                 std::string const &option,
                                 ModuleType moduleType,
                                 std::string const &modulePath,
                                 std::string const &moduleEntry) {
    std::string type_;
    switch (moduleType) {
    case C:
        type_ = "c";
        break;
    case CPP:
        type_ = "c++";
        break;
    case Python:
        type_ = "python";
        break;
    case Go:
        type_ = "go";
        break;
    }
    return bmf::BMFModule(moduleName, option, type_, modulePath, moduleEntry);
}

bmf::BMFCallback
GetCallbackInstance(std::function<bmf_sdk::CBytes(bmf_sdk::CBytes)> callback) {
    return bmf::BMFCallback(std::move(callback));
}

Stream::Stream(std::shared_ptr<internal::RealStream> baseP)
    : baseP_(std::move(baseP)) {}

void Stream::SetNotify(std::string const &notify) { baseP_->SetNotify(notify); }

void Stream::SetAlias(std::string const &alias) { baseP_->SetAlias(alias); }

void Stream::Start() { baseP_->Start(); }

Node Stream::Module(const std::vector<Stream> &inStreams,
                    std::string const &moduleName, ModuleType moduleType,
                    const bmf_sdk::JsonParam &option, std::string const &alias,
                    std::string const &modulePath,
                    std::string const &moduleEntry,
                    InputManagerType inputStreamManager, int scheduler) {
    return ConnectNewModule(alias, option, inStreams, moduleName, moduleType,
                            modulePath, moduleEntry, inputStreamManager,
                            scheduler);
}

Node Stream::CppModule(const std::vector<Stream> &inStreams,
                       std::string const &moduleName,
                       const bmf_sdk::JsonParam &option,
                       std::string const &alias, std::string const &modulePath,
                       std::string const &moduleEntry,
                       InputManagerType inputStreamManager, int scheduler) {
    return ConnectNewModule(alias, option, inStreams, moduleName, CPP,
                            modulePath, moduleEntry, inputStreamManager,
                            scheduler);
}

Node Stream::PythonModule(const std::vector<Stream> &inStreams,
                          std::string const &moduleName,
                          const bmf_sdk::JsonParam &option,
                          std::string const &alias,
                          std::string const &modulePath,
                          std::string const &moduleEntry,
                          InputManagerType inputStreamManager, int scheduler) {
    return ConnectNewModule(alias, option, inStreams, moduleName, Python,
                            modulePath, moduleEntry, inputStreamManager,
                            scheduler);
}

Node Stream::GoModule(const std::vector<Stream> &inStreams,
                      std::string const &moduleName,
                      const bmf_sdk::JsonParam &option,
                      std::string const &alias, std::string const &modulePath,
                      std::string const &moduleEntry,
                      InputManagerType inputStreamManager, int scheduler) {
    return ConnectNewModule(alias, option, inStreams, moduleName, Go,
                            modulePath, moduleEntry, inputStreamManager,
                            scheduler);
}

Node Stream::Decode(const bmf_sdk::JsonParam &decodePara,
                    std::string const &alias) {
    auto nd = ConnectNewModule(alias, decodePara, {}, "c_ffmpeg_decoder", CPP,
                               "", "", Immediate, 0);
    nd[0].SetNotify("video");
    nd[1].SetNotify("audio");
    return nd;
}

Node Stream::EncodeAsVideo(const bmf_sdk::JsonParam &encodePara,
                           std::string const &alias) {
    return ConnectNewModule(alias, encodePara, {}, "c_ffmpeg_encoder", CPP, "",
                            "", Immediate, 1);
}

Node Stream::EncodeAsVideo(Stream audioStream,
                           const bmf_sdk::JsonParam &encodePara,
                           std::string const &alias) {
    return ConnectNewModule(alias, encodePara, {std::move(audioStream)},
                            "c_ffmpeg_encoder", CPP, "", "", Immediate, 1);
}

Node Stream::FFMpegFilter(const std::vector<Stream> &inStreams,
                          std::string const &filterName,
                          bmf_sdk::JsonParam filterPara,
                          std::string const &alias) {
    nlohmann::json realPara;
    realPara["name"] = filterName;
    realPara["para"] = filterPara.json_value_;
    filterPara = bmf_sdk::JsonParam(realPara);
    return ConnectNewModule(alias, filterPara, inStreams, "c_ffmpeg_filter",
                            CPP, "", "", Immediate, 0);
}

Node Stream::Fps(int fps, std::string const &alias) {
    bmf_sdk::JsonParam para;
    para.json_value_["fps"] = fps;
    return FFMpegFilter({}, "fps", para, alias);
}

Node Stream::InternalFFMpegFilter(const std::vector<Stream> &inStreams,
                                  std::string const &filterName,
                                  const bmf_sdk::JsonParam &filterPara,
                                  std::string const &alias) {
    return ConnectNewModule(alias, filterPara, inStreams, "c_ffmpeg_filter",
                            CPP, "", "", Immediate, 0);
}

Node Stream::ConnectNewModule(
    const std::string &alias, const bmf_sdk::JsonParam &option,
    const std::vector<Stream> &inputStreams, const std::string &moduleName,
    ModuleType moduleType, const std::string &modulePath,
    const std::string &moduleEntry, InputManagerType inputStreamManager,
    int scheduler) {
    std::vector<std::shared_ptr<internal::RealStream>> inRealStreams;
    inRealStreams.reserve(inputStreams.size());
    for (auto &s : inputStreams)
        inRealStreams.emplace_back(s.baseP_);
    return Node(baseP_->AddModule(alias, option, inRealStreams, moduleName,
                                  moduleType, modulePath, moduleEntry,
                                  inputStreamManager, scheduler));
}

std::string Stream::GetName() { return baseP_->GetName(); }

Node::Node(std::shared_ptr<internal::RealNode> baseP)
    : baseP_(std::move(baseP)) {}

class Stream Node::operator[](int index) { return Stream(index); }

class Stream Node::operator[](std::string const &notifyOrAlias) {
    return Stream(notifyOrAlias);
}

class Stream Node::Stream(int index) {
    return (class Stream)(baseP_->Stream(index));
}

class Stream Node::Stream(std::string const &notifyOrAlias) {
    return (class Stream)(baseP_->Stream(notifyOrAlias));
}

Node::operator class Stream() { return Stream(0); }

void Node::SetAlias(std::string const &alias) { baseP_->SetAlias(alias); }

void Node::SetInputStreamManager(InputManagerType inputStreamManager) {
    baseP_->SetInputManager(inputStreamManager);
}

void Node::SetThread(int threadNum) { baseP_->SetScheduler(threadNum); }

void Node::SetPreModule(const bmf::BMFModule &preModuleInstance) {
    baseP_->SetPreModule(preModuleInstance);
}

void Node::AddCallback(long long key,
                       const bmf::BMFCallback &callbackInstance) {
    baseP_->AddCallback(key, callbackInstance);
}

void Node::AddCallback(long long key, 
                       std::function<bmf_sdk::CBytes(bmf_sdk::CBytes)> callback) {
    baseP_->AddCallback(key, callback);
}

void Node::Start() { Stream(0).Start(); }

Node Node::Module(const std::vector<class Stream> &inStreams,
                  std::string const &moduleName, ModuleType moduleType,
                  const bmf_sdk::JsonParam &option, std::string const &alias,
                  std::string const &modulePath, std::string const &moduleEntry,
                  InputManagerType inputStreamManager, int scheduler) {
    return ConnectNewModule(alias, option, inStreams, moduleName, moduleType,
                            modulePath, moduleEntry, inputStreamManager,
                            scheduler);
}

Node Node::CppModule(const std::vector<class Stream> &inStreams,
                     std::string const &moduleName,
                     const bmf_sdk::JsonParam &option, std::string const &alias,
                     std::string const &modulePath,
                     std::string const &moduleEntry,
                     InputManagerType inputStreamManager, int scheduler) {
    return ConnectNewModule(alias, option, inStreams, moduleName, CPP,
                            modulePath, moduleEntry, inputStreamManager,
                            scheduler);
}

Node Node::PythonModule(const std::vector<class Stream> &inStreams,
                        std::string const &moduleName,
                        const bmf_sdk::JsonParam &option,
                        std::string const &alias, std::string const &modulePath,
                        std::string const &moduleEntry,
                        InputManagerType inputStreamManager, int scheduler) {
    return ConnectNewModule(alias, option, inStreams, moduleName, Python,
                            modulePath, moduleEntry, inputStreamManager,
                            scheduler);
}

Node Node::GoModule(const std::vector<class Stream> &inStreams,
                    std::string const &moduleName,
                    const bmf_sdk::JsonParam &option, std::string const &alias,
                    std::string const &modulePath,
                    std::string const &moduleEntry,
                    InputManagerType inputStreamManager, int scheduler) {
    return ConnectNewModule(alias, option, inStreams, moduleName, Go,
                            modulePath, moduleEntry, inputStreamManager,
                            scheduler);
}

Node Node::Decode(const bmf_sdk::JsonParam &decodePara,
                  std::string const &alias) {
    auto nd = ConnectNewModule(alias, decodePara, {}, "c_ffmpeg_decoder", CPP,
                               "", "", Immediate, 0);
    nd[0].SetNotify("video");
    nd[1].SetNotify("audio");
    return nd;
}

Node Node::EncodeAsVideo(const bmf_sdk::JsonParam &encodePara,
                         std::string const &alias) {
    return ConnectNewModule(alias, encodePara, {}, "c_ffmpeg_encoder", CPP, "",
                            "", Immediate, 1);
}

Node Node::EncodeAsVideo(class Stream audioStream,
                         const bmf_sdk::JsonParam &encodePara,
                         std::string const &alias) {
    return ConnectNewModule(alias, encodePara, {std::move(audioStream)},
                            "c_ffmpeg_encoder", CPP, "", "", Immediate, 1);
}

Node Node::FFMpegFilter(const std::vector<class Stream> &inStreams,
                        std::string const &filterName,
                        bmf_sdk::JsonParam filterPara,
                        std::string const &alias) {
    nlohmann::json realPara;
    realPara["name"] = filterName;
    realPara["para"] = filterPara.json_value_;
    filterPara = bmf_sdk::JsonParam(realPara);
    return ConnectNewModule(alias, filterPara, inStreams, "c_ffmpeg_filter",
                            CPP, "", "", Immediate, 0);
}

Node Node::Fps(int fps, std::string const &alias) {
    bmf_sdk::JsonParam para;
    para.json_value_["fps"] = fps;
    return FFMpegFilter({}, "fps", para, alias);
}

Node Node::InternalFFMpegFilter(const std::vector<class Stream> &inStreams,
                                std::string const &filterName,
                                const bmf_sdk::JsonParam &filterPara,
                                std::string const &alias) {
    return ConnectNewModule(alias, filterPara, inStreams, "c_ffmpeg_filter",
                            CPP, "", "", Immediate, 0);
}

Node Node::ConnectNewModule(
    std::string const &alias, const bmf_sdk::JsonParam &option,
    const std::vector<class Stream> &inputStreams,
    std::string const &moduleName, ModuleType moduleType,
    std::string const &modulePath, std::string const &moduleEntry,
    InputManagerType inputStreamManager, int scheduler) {
    std::vector<std::shared_ptr<internal::RealStream>> inRealStreams;
    inRealStreams.reserve(inputStreams.size());
    for (auto &s : inputStreams)
        inRealStreams.emplace_back(s.baseP_);
    return Node(baseP_->AddModule(alias, option, inRealStreams, moduleName,
                                  moduleType, modulePath, moduleEntry,
                                  inputStreamManager, scheduler));
}

Graph::Graph(GraphMode runMode, bmf_sdk::JsonParam graphOption)
    : graph_(std::make_shared<internal::RealGraph>(runMode, graphOption)) {}

Graph::Graph(GraphMode runMode, nlohmann::json graphOption)
    : graph_(std::make_shared<internal::RealGraph>(
          runMode, bmf_sdk::JsonParam(graphOption))) {}

bmf::BMFGraph Graph::Instantiate(bool dumpGraph, bool needMerge) {
    return graph_->Instantiate(dumpGraph, needMerge);
}

bmf::BMFGraph Graph::Instance() { return graph_->Instance(); }

int Graph::Run(bool dumpGraph, bool needMerge) {
    return graph_->Run(dumpGraph, needMerge);
}

void Graph::Start(bool dumpGraph, bool needMerge) {
    graph_->Start(dumpGraph, needMerge);
}

void Graph::Start(std::vector<Stream> &generateStreams, bool dumpGraph,
                  bool needMerge) {
    std::vector<std::shared_ptr<internal::RealStream>> generateRealStreams;
    generateRealStreams.reserve(generateStreams.size());
    for (auto &s : generateStreams)
        generateRealStreams.emplace_back(s.baseP_);
    graph_->Start(generateRealStreams, dumpGraph, needMerge);
}

int Graph::Close() {
    return graph_->Close();
}

int Graph::ForceClose() {
    return graph_->ForceClose();
}

Packet Graph::Generate(std::string streamName, bool block) {
    return graph_->Generate(streamName, block);
}

void Graph::SetTotalThreadNum(int num) {
    graph_->graphOption_.json_value_["scheduler_count"] = num;
}

Stream Graph::NewPlaceholderStream() {
    return Stream(graph_->NewPlaceholderStream());
}

Node Graph::GetAliasedNode(std::string const &alias) {
    return Node(graph_->GetAliasedNode(alias));
}

Stream Graph::GetAliasedStream(std::string const &alias) {
    return Stream(graph_->GetAliasedStream(alias));
}

std::string Graph::Dump() { return graph_->Dump().dump(4); }

Node Graph::Module(const std::vector<Stream> &inStreams,
                   std::string const &moduleName, ModuleType moduleType,
                   const bmf_sdk::JsonParam &option, std::string const &alias,
                   std::string const &modulePath,
                   std::string const &moduleEntry,
                   InputManagerType inputStreamManager, int scheduler) {
    return NewNode(alias, option, inStreams, moduleName, moduleType, modulePath,
                   moduleEntry, inputStreamManager, scheduler);
}

Node Graph::CppModule(const std::vector<Stream> &inStreams,
                      std::string const &moduleName,
                      const bmf_sdk::JsonParam &option,
                      std::string const &alias, std::string const &modulePath,
                      std::string const &moduleEntry,
                      InputManagerType inputStreamManager, int scheduler) {
    return NewNode(alias, option, inStreams, moduleName, CPP, modulePath,
                   moduleEntry, inputStreamManager, scheduler);
}

Node Graph::PythonModule(const std::vector<Stream> &inStreams,
                         std::string const &moduleName,
                         const bmf_sdk::JsonParam &option,
                         std::string const &alias,
                         std::string const &modulePath,
                         std::string const &moduleEntry,
                         InputManagerType inputStreamManager, int scheduler) {
    return NewNode(alias, option, inStreams, moduleName, Python, modulePath,
                   moduleEntry, inputStreamManager, scheduler);
}

Node Graph::GoModule(const std::vector<Stream> &inStreams,
                     std::string const &moduleName,
                     const bmf_sdk::JsonParam &option, std::string const &alias,
                     std::string const &modulePath,
                     std::string const &moduleEntry,
                     InputManagerType inputStreamManager, int scheduler) {
    return NewNode(alias, option, inStreams, moduleName, Go, modulePath,
                   moduleEntry, inputStreamManager, scheduler);
}

Node Graph::Decode(const bmf_sdk::JsonParam &decodePara,
                   std::string const &alias,
                   int scheduler) {
    auto nd = NewNode(alias, decodePara, {}, "c_ffmpeg_decoder", CPP, "", "",
                      Immediate, scheduler);
    nd[0].SetNotify("video");
    nd[1].SetNotify("audio");
    return nd;
}

Node Graph::Decode(const bmf_sdk::JsonParam &decodePara, Stream controlStream,
                   std::string const &alias,
                   int scheduler) {
    return NewNode(alias, decodePara, {std::move(controlStream)},
                   "c_ffmpeg_decoder", CPP, "", "", Immediate, scheduler);
}

Node Graph::Encode(Stream videoStream, Stream audioStream,
                   const bmf_sdk::JsonParam &encodePara,
                   std::string const &alias,
                   int scheduler) {
    return NewNode(alias, encodePara,
                   {std::move(videoStream), std::move(audioStream)},
                   "c_ffmpeg_encoder", CPP, "", "", Immediate, scheduler);
}

Node Graph::Encode(Stream videoStream, const bmf_sdk::JsonParam &encodePara,
                   std::string const &alias,
                   int scheduler) {
    return NewNode(alias, encodePara, {std::move(videoStream)},
                   "c_ffmpeg_encoder", CPP, "", "", Immediate, scheduler);
}

Node Graph::Encode(const bmf_sdk::JsonParam &encodePara,
                   std::string const &alias,
                   int scheduler) {
    return NewNode(alias, encodePara, {}, "c_ffmpeg_encoder", CPP, "", "",
                   Immediate, scheduler);
}

Node Graph::FFMpegFilter(const std::vector<Stream> &inStreams,
                         std::string const &filterName,
                         const bmf_sdk::JsonParam &filterPara,
                         std::string const &alias) {
    nlohmann::json realPara;
    realPara["name"] = filterName;
    realPara["para"] = filterPara.json_value_;
    return NewNode(alias, bmf_sdk::JsonParam(realPara), inStreams,
                   "c_ffmpeg_filter", CPP, "", "", Immediate, 0);
}

Node Graph::Fps(Stream inStream, int fps, std::string const &alias) {
    bmf_sdk::JsonParam para;
    para.json_value_["fps"] = fps;
    return FFMpegFilter({std::move(inStream)}, "fps", para, alias);
}

Node Graph::InternalFFMpegFilter(const std::vector<Stream> &inStreams,
                                 std::string const &filterName,
                                 const bmf_sdk::JsonParam &filterPara,
                                 std::string const &alias) {
    return NewNode(alias, filterPara, inStreams, "c_ffmpeg_filter", CPP, "", "",
                   Immediate, 0);
}

Node Graph::NewNode(std::string const &alias, const bmf_sdk::JsonParam &option,
                    const std::vector<Stream> &inputStreams,
                    std::string const &moduleName, ModuleType moduleType,
                    std::string const &modulePath,
                    std::string const &moduleEntry,
                    InputManagerType inputStreamManager, int scheduler) {
    std::vector<std::shared_ptr<internal::RealStream>> inRealStreams;
    inRealStreams.reserve(inputStreams.size());
    for (auto &s : inputStreams)
        inRealStreams.emplace_back(s.baseP_);
    return Node(graph_->AddModule(alias, option, inRealStreams, moduleName,
                                  moduleType, modulePath, moduleEntry,
                                  inputStreamManager, scheduler));
}

SyncModule Graph::Sync(const std::vector<int> inStreams,
                       const std::vector<int> outStreams,
                       bmf_sdk::JsonParam moduleOption,
                       std::string const &moduleName, ModuleType moduleType,
                       std::string const &modulePath,
                       std::string const &moduleEntry, std::string const &alias,
                       InputManagerType inputStreamManager, int scheduler) {
    auto sync_m = SyncModule();
    std::string module_type;
    switch (moduleType) {
    case C:
        module_type = "c";
        break;
    case Python:
        module_type = "python";
        break;
    case Go:
        module_type = "go";
        break;
    default:
        module_type = "c++";
    }
    if (moduleName.compare("c_ffmpeg_filter") == 0) {
        nlohmann::json inputOption;
        nlohmann::json outputOption;
        for (auto id : inStreams) {
            nlohmann::json stream = {
                {"identifier", moduleName + std::to_string(id)}};
            inputOption.push_back(stream);
        }
        for (auto id : outStreams) {
            nlohmann::json stream = {
                {"identifier", moduleName + std::to_string(id)}};
            outputOption.push_back(stream);
        }
        nlohmann::json option = {
            {"option", moduleOption.json_value_},
            {"input_streams", inputOption},
            {"output_streams", outputOption},
        };
        auto config = bmf_engine::NodeConfig(option);
        bmf_engine::Optimizer::convert_filter_para(config);
        bmf_engine::Optimizer::replace_stream_name_with_id(config);
        moduleOption = config.get_option();
    }
    bmf_engine::ModuleFactory::create_module(
        moduleName, -1, moduleOption, module_type, modulePath, moduleEntry,
        sync_m.moduleInstance);
    sync_m.inputStreams = inStreams;
    sync_m.outputStreams = outStreams;
    sync_m.moduleInstance->init();
    return sync_m;
}

SyncModule Graph::Sync(const std::vector<int> inStreams,
                       const std::vector<int> outStreams,
                       nlohmann::json moduleOption,
                       std::string const &moduleName, ModuleType moduleType,
                       std::string const &modulePath,
                       std::string const &moduleEntry, std::string const &alias,
                       InputManagerType inputStreamManager, int scheduler) {
    return Sync(inStreams, outStreams, bmf_sdk::JsonParam(moduleOption),
                moduleName, moduleType, modulePath, moduleEntry, alias,
                inputStreamManager, scheduler);
}

std::map<int, std::vector<Packet>>
Graph::Process(SyncModule module,
               std::map<int, std::vector<Packet>> inputPackets) {
    auto task = bmf_sdk::Task(0, module.inputStreams, module.outputStreams);
    for (auto const &pkts : inputPackets) {
        for (auto const &pkt : pkts.second) {
            task.fill_input_packet(pkts.first, pkt);
        }
    }
    module.moduleInstance->process(task);
    std::map<int, std::vector<Packet>> returnMap;
    for (auto id : module.outputStreams) {
        auto it = task.outputs_queue_.find(id);
        if (it == task.outputs_queue_.end())
            continue;
        while (!it->second->empty()) {
            Packet pkt;
            task.pop_packet_from_out_queue(id, pkt);
            returnMap[id].push_back(pkt);
        }
    }
    return returnMap;
}

SyncPackets Graph::Process(SyncModule module, SyncPackets pkts) {
    SyncPackets returnPkts;
    returnPkts.packets = Process(module, pkts.packets);
    return returnPkts;
}

int32_t Graph::Init(SyncModule module) { return module.moduleInstance->init(); }

int32_t Graph::Close(SyncModule module) {
    return module.moduleInstance->close();
}

int32_t Graph::SendEOF(SyncModule module) {
    auto task = bmf_sdk::Task(0, module.inputStreams, module.outputStreams);
    for (auto id : module.inputStreams) {
        task.fill_input_packet(id, Packet::generate_eof_packet());
    }
    return module.moduleInstance->process(task);
}

void Graph::SetOption(const bmf_sdk::JsonParam &optionPatch) {
    graph_->SetOption(optionPatch);
}

Stream Graph::InputStream(std::string streamName, std::string notify,
                          std::string alias) {
    return Stream(graph_->InputStream(streamName, notify, alias));
}

int Graph::FillPacket(std::string streamName, Packet packet, bool block) {
    return graph_->FillPacket(streamName, packet, block);
}


static inline bool check_graph_instance(const std::shared_ptr<bmf::BMFGraph>& graph_instance, const char* func_name) { 
    if (!graph_instance) {
        BMFLOG(BMF_ERROR) << "[builder::Graph::" << func_name << "] BMFGraph instance not initialized";
        return false;
    }
    return true;
}

// ----------------- update -----------------
int Graph::update(const bmf_sdk::JsonParam& update_config) {
    if (!check_graph_instance(graph_->graphInstance_, "update")) {
        return -1;
    }

    try {
        JsonParam new_update_config = update_config;
        if (new_update_config.json_value_.contains("nodes") &&
            new_update_config.json_value_["nodes"].is_array()) {

            auto& nodes = new_update_config.json_value_["nodes"];
            for (auto& node : nodes) {
                std::shared_ptr<internal::RealNode> matched_node = nullptr;

                // 处理 add 节点：仅补全基础信息，流绑定交给 engine 层
                if (node.contains("action") && node["action"] == "add") {
                    // 补全 module_info（若未传递，复用同类节点默认值，避免 engine 层报错）
                    if (!node.contains("module_info") || !node["module_info"].is_object()) {
                        BMFLOG(BMF_WARNING) << "[update] add节点未传递 module_info，使用默认c++模块配置";
                        node["module_info"] = json({
                            {"name", "default_module"},
                            {"type", "c++"},
                            {"path", ""},
                            {"entry", ""}
                        });
                    }

                    // 补全 input_manager（默认 immediate）
                    if (!node.contains("input_manager") || !node["input_manager"].is_string()) {
                        node["input_manager"] = "immediate";
                        BMFLOG(BMF_INFO) << "[update] add节点补全 input_manager: immediate";
                    }

                    // 补全 scheduler（默认 0）
                    if (!node.contains("scheduler") || !node["scheduler"].is_number()) {
                        node["scheduler"] = 0;
                        BMFLOG(BMF_INFO) << "[update] add节点补全 scheduler: 0";
                    }

                    // 确保 option 是对象（避免 JSON null 错误）
                    if (!node.contains("option") || !node["option"].is_object()) {
                        node["option"] = json::object();
                        BMFLOG(BMF_INFO) << "[update] add节点初始化 option: 空对象";
                    }
                    continue; // 跳过 reset 逻辑
                }

                // 处理 reset 节点,补全 id（通过 alias 查找原节点，同时保存匹配的节点指针）
                if ((!node.contains("id") || node["id"].is_null()) &&
                    node.contains("action") && node["action"] == "reset" &&
                    node.contains("alias")) {

                    std::string alias = node["alias"].get<std::string>();
                    for (auto &real_node : graph_->nodes_) {
                        if (real_node && real_node->alias_ == alias) {
                            node["id"] = real_node->id_;
                            matched_node = real_node;
                            BMFLOG(BMF_INFO) << "[update] reset节点补全id: " << alias << " → " << real_node->id_;
                            break;
                        }
                    }
                }
                // 2. 校验 id（底层必需）
                if (!node.contains("id")) {
                    BMFLOG(BMF_ERROR) << "[update] 节点缺少id，跳过";
                    continue;
                }
                int node_id = node["id"].get<int>();

                // 3. 查找原节点（复用第一次匹配的结果）
                std::shared_ptr<internal::RealNode> real_node = matched_node;
                if (!real_node) { // 如果第一次未匹配（通过id直接更新的场景），再遍历查找
                    for (auto &n : graph_->nodes_) {
                        if (n && n->id_ == node_id) {
                            real_node = n;
                            break;
                        }
                    }
                }
                if (!real_node) {
                    BMFLOG(BMF_ERROR) << "[update] 找不到节点: " << node_id;
                    continue;
                }

                // 补全 module_info（复用原节点）
                if (!node.contains("module_info") || !node["module_info"].is_object()) {
                    auto& module = real_node->moduleInfo_;
                    std::string module_type_str;
                    switch (module.moduleType_) {
                        case CPP: module_type_str = "c++"; break;
                        case Python: module_type_str = "python"; break;
                        case C: module_type_str = "c"; break;
                        case Go: module_type_str = "go"; break;
                        default: module_type_str = "";
                    }
                    node["module_info"] = json({
                        {"name", module.moduleName_},
                        {"type", module_type_str},
                        {"path", module.modulePath_},
                        {"entry", module.moduleEntry_}
                    });
                    BMFLOG(BMF_INFO) << "[update] reset节点补全 module_info: node_id=" << node_id;
                }

                // 补全 input_manager（复用原节点）
                if (!node.contains("input_manager") || !node["input_manager"].is_string()) {
                    std::string im_str;
                    switch (real_node->inputManager_) {
                        case Immediate: im_str = "immediate"; break;
                        case Default: im_str = "default"; break;
                        case Server: im_str = "server"; break;
                        default: im_str = "default";
                    }
                    node["input_manager"] = im_str;
                    BMFLOG(BMF_INFO) << "[update] reset节点补全 input_manager: node_id=" << node_id;
                }

                // 补全 scheduler（复用原节点）
                if (!node.contains("scheduler") || !node["scheduler"].is_number()) {
                    node["scheduler"] = real_node->scheduler_;
                    BMFLOG(BMF_INFO) << "[update] reset节点补全 scheduler: node_id=" << node_id;
                }

                // 确保 option 是对象（避免 JSON null 错误）
                if (!node.contains("option") || !node["option"].is_object()) {
                    node["option"] = json::object();
                    BMFLOG(BMF_INFO) << "[update] reset节点初始化 option: node_id=" << node_id;
                }
            }
        }

        // 调用底层 update（传递完整 config）
        std::string config_str = new_update_config.json_value_.dump();
        BMFLOG(BMF_INFO) << "[update] 传递给 engine 层的配置:\n" << config_str;
        graph_->graphInstance_->update(config_str, false);
        BMFLOG(BMF_INFO) << "[update] 成功";

        return 0;
    } catch (const std::exception& e) {
        BMFLOG(BMF_ERROR) << "[update] 异常: " << e.what();
        return -1;
    }
}

// 动态添加节点
int Graph::dynamic_add_node(const bmf_sdk::JsonParam &node_config) {
    try {
        // 1. 基础校验
        if (!graph_ || graph_->nodes_.empty()) { 
            BMFLOG(BMF_ERROR) << "[dynamic_add_node] 主图未初始化或无初始节点，无法添加新节点";
            return -1;
        }
        const auto &input_json = node_config.json_value_;
        if (!input_json.is_object()) {
            BMFLOG(BMF_ERROR) << "[dynamic_add_node] 参数必须是 JSON 对象";
            return -1;
        }

        // 必选字段校验（参考代码的结构化校验）
        std::string node_alias = input_json.value("alias", "");
        if (node_alias.empty()) {
            BMFLOG(BMF_ERROR) << "[dynamic_add_node] 缺少必选字段 alias";
            return -1;
        }
        if (!input_json.contains("module_info") || !input_json["module_info"].is_object()) {
            BMFLOG(BMF_ERROR) << "[dynamic_add_node] 缺少必选字段 module_info（需为对象）";
            return -1;
        }

        // 2. 结构化解析流配置（参考代码的 StreamConfig 逻辑，统一管理流参数）
        struct StreamConfig {
            std::string alias;    // 流别名
            std::string identifier;// 流标识（格式：{target_alias}.{id}_{i}）
            std::string notify;   // 流通知标识
        };
        std::vector<StreamConfig> input_stream_cfgs;
        std::vector<StreamConfig> output_stream_cfgs;

        // 2.1 解析输入流
        if (input_json.contains("input_streams") && input_json["input_streams"].is_array()) {
            for (const auto &stream_json : input_json["input_streams"]) {
                StreamConfig cfg;
                cfg.alias = stream_json.value("alias", "");
                cfg.identifier = stream_json.value("identifier", "");
                cfg.notify = stream_json.value("notify", "");

                // 校验流标识（非空+格式校验，确保符合 engine 层要求）
                if (cfg.identifier.empty()) {
                    BMFLOG(BMF_ERROR) << "[dynamic_add_node] 输入流 identifier 为空，跳过该流";
                    continue;
                }
                if (cfg.identifier.find('.') == std::string::npos) {
                    BMFLOG(BMF_ERROR) << "[dynamic_add_node] 输入流标识格式错误（需含'.'，如 pass_through.0_0）: " << cfg.identifier;
                    return -1;
                }

                // 校验目标节点是否存在（提前定位，避免 engine 层绑定失败）
                std::string target_alias = cfg.identifier.substr(0, cfg.identifier.find('.'));
                bool target_exist = false;
                for (auto &node : graph_->nodes_) { 
                    if (node && node->alias_ == target_alias) {
                        target_exist = true;
                        break;
                    }
                }
                if (!target_exist) {
                    BMFLOG(BMF_ERROR) << "[dynamic_add_node] 输入流目标节点不存在（alias=" << target_alias << "），流标识=" << cfg.identifier;
                    return -1;
                }

                input_stream_cfgs.push_back(cfg);
                BMFLOG(BMF_DEBUG) << "[dynamic_add_node] 解析输入流: alias=" << cfg.alias 
                                 << ", identifier=" << cfg.identifier << ", target_alias=" << target_alias;
            }
        }

        // 2.2 解析输出流
        if (input_json.contains("output_streams") && input_json["output_streams"].is_array()) {
            for (const auto &stream_json : input_json["output_streams"]) {
                StreamConfig cfg;
                cfg.alias = stream_json.value("alias", "");
                cfg.identifier = stream_json.value("identifier", "");
                cfg.notify = stream_json.value("notify", "");

                // 同样的校验逻辑（非空+格式+目标节点存在）
                if (cfg.identifier.empty()) {
                    BMFLOG(BMF_ERROR) << "[dynamic_add_node] 输出流 identifier 为空，跳过该流";
                    continue;
                }
                if (cfg.identifier.find('.') == std::string::npos) {
                    BMFLOG(BMF_ERROR) << "[dynamic_add_node] 输出流标识格式错误（需含'.'，如 pass_through.0_0）: " << cfg.identifier;
                    return -1;
                }

                std::string target_alias = cfg.identifier.substr(0, cfg.identifier.find('.'));
                bool target_exist = false;
                for (auto &node : graph_->nodes_) { // 同样访问graph_->nodes_
                    if (node && node->alias_ == target_alias) {
                        target_exist = true;
                        break;
                    }
                }
                if (!target_exist) {
                    BMFLOG(BMF_ERROR) << "[dynamic_add_node] 输出流目标节点不存在（alias=" << target_alias << "），流标识=" << cfg.identifier;
                    return -1;
                }

                output_stream_cfgs.push_back(cfg);
                BMFLOG(BMF_DEBUG) << "[dynamic_add_node] 解析输出流: alias=" << cfg.alias 
                                 << ", identifier=" << cfg.identifier << ", target_alias=" << target_alias;
            }
        }

        // 3. 生成唯一节点ID
        int new_node_id = 0;
        for (const auto &exist_node : graph_->nodes_) { 
            if (exist_node && exist_node->id_ > new_node_id) {
                new_node_id = exist_node->id_;
            }
        }
        new_node_id += 1; 
        BMFLOG(BMF_INFO) << "[dynamic_add_node] 分配新节点ID: " << new_node_id 
                         << "（现有最大节点ID: " << new_node_id - 1 << "）";

        // 4. 构造节点 JSON
        json node_json;
        node_json["action"] = "add";                  // 标记添加动作
        node_json["id"] = new_node_id;                // 内联生成的节点ID
        node_json["alias"] = node_alias;              // 节点别名
        node_json["module_info"] = input_json["module_info"]; // 模块信息
        node_json["option"] = input_json.value("option", json::object()); // 节点配置

        // 补全默认字段
        node_json["input_manager"] = input_json.value("input_manager", "immediate");
        node_json["scheduler"] = input_json.value("scheduler", 0);
        node_json["meta_info"] = input_json.value("meta_info", json({
            {"callback_binding", json::array()},
            {"premodule_id", -1}
        }));

        // 填充流配置（递原始信息给 engine 层）
        json input_streams_json = json::array();
        for (const auto &cfg : input_stream_cfgs) {
            input_streams_json.push_back({
                {"alias", cfg.alias},
                {"identifier", cfg.identifier},
                {"notify", cfg.notify}
            });
        }
        node_json["input_streams"] = input_streams_json;

        json output_streams_json = json::array();
        for (const auto &cfg : output_stream_cfgs) {
            output_streams_json.push_back({
                {"alias", cfg.alias},
                {"identifier", cfg.identifier},
                {"notify", cfg.notify}
            });
        }
        node_json["output_streams"] = output_streams_json;

        // 5. 构造 update 配置（参考代码的完整结构，对齐 Python 层 mode 字段）
        json update_graph_json;
        update_graph_json["input_streams"] = json::array();  // 空数组（engine 层自动处理）
        update_graph_json["output_streams"] = json::array(); // 空数组（engine 层自动处理）
        update_graph_json["option"] = json::object();        // 空对象（engine 层补全）
        update_graph_json["nodes"] = json::array({node_json});// 仅包含当前添加节点
        update_graph_json["mode"] = "Normal";             

        // 打印配置日志
        bmf_sdk::JsonParam update_cfg(update_graph_json);
        BMFLOG(BMF_INFO) << "[dynamic_add_node] 构造 update 配置（与 Python 对齐）:\n"
                         << update_cfg.json_value_.dump(4);

        // 6. 调用 builder 层 update
        int update_ret = this->update(update_cfg);
        if (update_ret != 0) {
            BMFLOG(BMF_ERROR) << "[dynamic_add_node] 调用主图 update 失败，ret=" << update_ret;
            return update_ret;
        }

        // 7. 结果反馈
        BMFLOG(BMF_INFO) << "[dynamic_add_node] 节点添加成功: alias=" << node_alias 
                         << ", node_id=" << new_node_id 
                         << ", 输入流数=" << input_stream_cfgs.size()
                         << ", 输出流数=" << output_stream_cfgs.size();
        return 0;

    } catch (const std::exception &e) {
        // 异常处理
        BMFLOG(BMF_ERROR) << "[dynamic_add_node] 异常: " << e.what();
        return -1;
    }
}

// ----------------- 动态删除节点 -----------------
int Graph::dynamic_remove_node(const bmf_sdk::JsonParam& node_config) {
    try {
        // 1. 核心校验（配置必须是对象+含id/alias，底层定位节点用）
        if (!node_config.json_value_.is_object() || 
            (!node_config.json_value_.contains("id") && !node_config.json_value_.contains("alias"))) {  // 必须包含id或alias（用于定位节点）
            BMFLOG(BMF_ERROR) << "[dynamic_remove_node] 配置缺少id/alias";
            return -1;
        }

        // 2. 构造删除配置（固定action=remove）
        JsonParam update_cfg;
        update_cfg.json_value_["nodes"] = nlohmann::json::array({node_config.json_value_}); // 将输入配置放入nodes数组 
        update_cfg.json_value_["nodes"][0]["action"] = "remove";  // 标记为删除操作
        update_cfg.json_value_["option"] = nlohmann::json::object();

        // 3. 调用update执行删除
        BMFLOG(BMF_INFO) << "[dynamic_remove_node] 调用update配置:\n" << update_cfg.json_value_.dump(2);
        return update(update_cfg);

    } catch (const std::exception& e) {
        BMFLOG(BMF_ERROR) << "[dynamic_remove_node] 异常: " << e.what();
        return -1;
    }
}

// 动态重置节点
int Graph::dynamic_reset_node(const bmf_sdk::JsonParam &node_config) {
    try {
        // 1. 校验必要字段：仅需 alias 或 id（与 Python 一致）
        if (!node_config.json_value_.is_object() ||
            (!node_config.json_value_.contains("alias") &&
             !node_config.json_value_.contains("id"))) {
            BMFLOG(BMF_ERROR) << "[builder.dynamic_reset_node] 配置无效，缺少 alias 或 id";
            return -1;
        }

        // 2. 构造极简的 reset 节点配置（仅含 action + alias/id + option）
        nlohmann::json node_json;
        node_json["action"] = "reset";  // 必选：标记重置动作

        // 传递 alias 或 id（定位节点，二选一）
        if (node_config.json_value_.contains("alias")) {
            node_json["alias"] = node_config.json_value_["alias"];
        }
        if (node_config.json_value_.contains("id")) {
            node_json["id"] = node_config.json_value_["id"];
        }

        nlohmann::json option_json = node_config.json_value_;
        option_json.erase("alias");
        option_json.erase("id");
        node_json["option"] = option_json;  

        // 3. 构造 update_graph（移除冗余的 mode 设置，继承主图模式）
        nlohmann::json update_graph_json;
        update_graph_json["input_streams"] = nlohmann::json::array();  // 空数组
        update_graph_json["output_streams"] = nlohmann::json::array(); // 空数组
        update_graph_json["option"] = nlohmann::json::object();        // 空对象
        update_graph_json["nodes"] = nlohmann::json::array({node_json}); // 仅 1 个重置节点

        BMFLOG(BMF_INFO) << "[builder.dynamic_reset_node] 构造的 update graph:\n"
                         << update_graph_json.dump(2);

        // 4. 调用 update，由底层补全其他字段
        bmf_sdk::JsonParam update_config(update_graph_json);
        int ret = this->update(update_config);

        if (ret != 0) {
            BMFLOG(BMF_ERROR) << "[builder.dynamic_reset_node] 调用主图 update 失败 ret=" << ret;
        } else {
            std::string alias = node_config.json_value_.value("alias", "");
            BMFLOG(BMF_INFO) << "[builder.dynamic_reset_node] dynamic reset success for alias=" << alias;
        }
        return ret;
    } catch (const std::exception &e) {
        BMFLOG(BMF_ERROR) << "[builder.dynamic_reset_node] 异常: " << e.what();
        return -1;
    }
}


/*
static inline bool check_graph_instance(const std::shared_ptr<bmf::BMFGraph>& graph_instance, const char* func_name) { 
    if (!graph_instance) {  // 检查底层BMFGraph实例是否为空
        BMFLOG(BMF_ERROR) << "[builder::Graph::" << func_name << "] BMFGraph instance not initialized";
        return false;
    }
    return true;
}

// ----------------- update -----------------
int Graph::update(const bmf_sdk::JsonParam& update_config) {
    // 1. 底层实例检查
    if (!check_graph_instance(graph_->graphInstance_, "update")) {
        return -1;
    }

    try {
        // 2. 配置必要修复（仅保留底层必需的id检查和module_info补全）
        JsonParam new_update_config = update_config;
        if (new_update_config.json_value_.contains("nodes") && new_update_config.json_value_["nodes"].is_array()) {
            auto& nodes = new_update_config.json_value_["nodes"];
            for (auto& node : nodes) {
                // 必需：节点必须有id（底层定位节点用）
                if (!node.contains("id")) {
                    BMFLOG(BMF_ERROR) << "[update] 节点缺少id字段，跳过该节点";
                    continue;
                }
                // 必需：补全module_info（底层要求非空对象，避免序列化错误）
                if (!node.contains("module_info") || !node["module_info"].is_object()) {
                    BMFLOG(BMF_INFO) << "[update] 节点" << node["id"].get<int>() << "补全module_info";
                    node["module_info"] = nlohmann::json({  // // 补全默认的module_info
                        {"name", "pass_through"},  // 通用默认模块
                        {"type", "c++"},
                        {"path", ""},
                        {"entry", ""}
                    });
                }
            }
        }

        // 3. 构造底层配置并调用update
        bmf_engine::GraphConfig graph_cfg(new_update_config.json_value_);  // 将修复后的配置转换为底层引擎需要的GraphConfig对象
        std::string config_str = graph_cfg.to_json().dump();  // 转换为JSON字符串，便于日志输出和底层处理
        BMFLOG(BMF_INFO) << "[update] 底层配置: " << config_str;  // 输出最终发送到底层的配置，方便调试

        graph_->graphInstance_->update(config_str, false);  //// 调用底层实例的update方法
        BMFLOG(BMF_INFO) << "[update] 成功";
        return 0;

    } catch (const std::exception& e) {
        BMFLOG(BMF_ERROR) << "[update] 异常: " << e.what();
        return -1;
    }
}

// 动态添加节点
int Graph::dynamic_add_node(const bmf_sdk::JsonParam& node_config) {
    try {
        // 1. 提取节点配置（支持根节点或nodes数组，通用场景）
        JsonParam update_cfg;
        nlohmann::json target_node;  // 目标节点的配置
        if (node_config.json_value_.contains("nodes") && node_config.json_value_["nodes"].is_array()) {  // 若输入配置包含nodes数组
            target_node = node_config.json_value_["nodes"][0];  // 取第一个节点作为目标节点
        } else {
            target_node = node_config.json_value_;  // 若输入是单个节点配置，直接作为目标
        }

        // 2. 补全必填字段（底层要求，缺一不可）
        if (!target_node.contains("action")) target_node["action"] = "add";  // 固定为add
        if (!target_node.contains("scheduler")) target_node["scheduler"] = 0;  // 默认调度器
        if (!target_node.contains("input_manager")) target_node["input_manager"] = "immediate";  // 默认输入管理为"immediate"（立即处理输入）
        // 补全meta_info（避免底层序列化null崩溃）
        if (!target_node.contains("meta_info") || !target_node["meta_info"].is_object()) {
            target_node["meta_info"] = nlohmann::json({
                {"premodule_id", -1},
                {"callback_binding", nlohmann::json::array()},
                {"queue_length_limit", 5}
            });
        }
        // 补全module_info（滤镜节点专用，避免重复调用时缺失）
        if (!target_node.contains("module_info") || !target_node["module_info"].is_object()) {
            target_node["module_info"] = nlohmann::json({
                {"name", "c_ffmpeg_filter"},
                {"type", "c++"},
                {"path", "/root/bmf/output/bmf/lib/libbuiltin_modules.so"},
                {"entry", ""}
            });
        }
        // 补全output_streams.identifier（避免null）
        if (target_node.contains("output_streams") && target_node["output_streams"].is_array()) {
            for (auto& os : target_node["output_streams"]) {
                if (!os.contains("identifier")) {
                    os["identifier"] = "c_ffmpeg_filter_" + std::to_string(target_node["id"].get<int>()) + "_0";
                }
            }
        }

        // 3. 构造update配置并调用
        update_cfg.json_value_["nodes"] = nlohmann::json::array({target_node});   // 将目标节点放入nodes数组（符合底层要求的格式）
        update_cfg.json_value_["option"] = nlohmann::json::object();

        BMFLOG(BMF_INFO) << "[dynamic_add_node] 调用update配置:\n" << update_cfg.json_value_.dump(2);
        return update(update_cfg);  // 调用update方法执行添加操作

    } catch (const std::exception& e) {
        BMFLOG(BMF_ERROR) << "[dynamic_add_node] 异常: " << e.what();
        return -1;
    }
}

// ----------------- 动态删除节点 -----------------
int Graph::dynamic_remove_node(const bmf_sdk::JsonParam& node_config) {
    try {
        // 1. 核心校验（配置必须是对象+含id/alias，底层定位节点用）
        if (!node_config.json_value_.is_object() || 
            (!node_config.json_value_.contains("id") && !node_config.json_value_.contains("alias"))) {  // 必须包含id或alias（用于定位节点）
            BMFLOG(BMF_ERROR) << "[dynamic_remove_node] 配置缺少id/alias";
            return -1;
        }

        // 2. 构造删除配置（固定action=remove）
        JsonParam update_cfg;
        update_cfg.json_value_["nodes"] = nlohmann::json::array({node_config.json_value_}); // 将输入配置放入nodes数组 
        update_cfg.json_value_["nodes"][0]["action"] = "remove";  // 标记为删除操作
        update_cfg.json_value_["option"] = nlohmann::json::object();

        // 3. 调用update执行删除
        BMFLOG(BMF_INFO) << "[dynamic_remove_node] 调用update配置:\n" << update_cfg.json_value_.dump(2);
        return update(update_cfg);

    } catch (const std::exception& e) {
        BMFLOG(BMF_ERROR) << "[dynamic_remove_node] 异常: " << e.what();
        return -1;
    }
}

// ----------------- 动态重置节点 -----------------
int Graph::dynamic_reset_node(const bmf_sdk::JsonParam& node_config) {
    try {
        // 1. 核心校验（配置必须是对象+含id/alias+含option，底层重置参数用）
        if (!node_config.json_value_.is_object() || // 必须是JSON对象
            !node_config.json_value_.contains("option") ||  // 必须包含option（重置的参数） 
            (!node_config.json_value_.contains("id") && !node_config.json_value_.contains("alias"))) {
            BMFLOG(BMF_ERROR) << "[dynamic_reset_node] 配置无效（需对象+id/alias+option）";
            return -1;
        }

        // 2. 构造重置配置（固定action=reset）
        JsonParam update_cfg;
        update_cfg.json_value_["nodes"] = nlohmann::json::array({node_config.json_value_});
        update_cfg.json_value_["nodes"][0]["action"] = "reset";  // 标记为重置操作
        update_cfg.json_value_["option"] = nlohmann::json::object();

        // 3. 调用update执行重置
        BMFLOG(BMF_INFO) << "[dynamic_reset_node] 调用update配置:\n" << update_cfg.json_value_.dump(2);
        return update(update_cfg);

    } catch (const std::exception& e) {
        BMFLOG(BMF_ERROR) << "[dynamic_reset_node] 异常: " << e.what();
        return -1;
    }
}*/

static inline bool check_graph_instance(const std::shared_ptr<bmf::BMFGraph>& graph_instance, const char* func_name) { 
    if (!graph_instance) {  // 检查底层BMFGraph实例是否为空
        BMFLOG(BMF_ERROR) << "[builder::Graph::" << func_name << "] BMFGraph instance not initialized";
        return false;
    }
    return true;
}

void SyncPackets::Insert(int streamId, std::vector<Packet> frames) {
    packets.insert(std::make_pair(streamId, frames));
}

std::vector<Packet> SyncPackets::operator[](int index) {
    return packets[index];
}

std::map<int, std::vector<Packet>>
SyncModule::ProcessPkts(std::map<int, std::vector<Packet>> inputPackets) {
    auto task = bmf_sdk::Task(0, inputStreams, outputStreams);
    for (auto const &pkts : inputPackets) {
        for (auto const &pkt : pkts.second) {
            task.fill_input_packet(pkts.first, pkt);
        }
    }
    moduleInstance->process(task);
    std::map<int, std::vector<Packet>> returnMap;
    for (auto id : outputStreams) {
        auto it = task.outputs_queue_.find(id);
        if (it == task.outputs_queue_.end())
            continue;
        while (!it->second->empty()) {
            Packet pkt;
            task.pop_packet_from_out_queue(id, pkt);
            returnMap[id].push_back(pkt);
        }
    }
    return returnMap;
}

SyncPackets SyncModule::ProcessPkts(SyncPackets pkts) {
    SyncPackets returnPkts;
    returnPkts.packets = ProcessPkts(pkts.packets);
    return returnPkts;
}

int32_t SyncModule::Process(bmf_sdk::Task task) {
    return moduleInstance->process(task);
}

int32_t SyncModule::SendEOF() {
    auto task = bmf_sdk::Task(0, inputStreams, outputStreams);
    for (auto id : inputStreams) {
        task.fill_input_packet(id, Packet::generate_eof_packet());
    }
    return moduleInstance->process(task);
}

int32_t SyncModule::Init() { return moduleInstance->init(); }

int32_t SyncModule::Close() { return moduleInstance->close(); }

void SyncModule::DynamicReset(const bmf_sdk::JsonParam& opt_reset) {
    moduleInstance->dynamic_reset(opt_reset);
}
} // namespace bmf::builder

