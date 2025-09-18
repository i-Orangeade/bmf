#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "../include/graph.h"

USE_BMF_ENGINE_NS

using json = nlohmann::json;

// Test: dynamic add / reset / remove nodes
TEST(CppDynamicUpdate, AddResetRemove) {
    BMFLOG_SET_LEVEL(BMF_INFO);
   
    GraphConfig cfg;
    cfg.option.json_value_ = json::object();
    std::map<int, std::shared_ptr<Module>> pre_modules;
    std::map<int, std::shared_ptr<ModuleCallbackLayer>> callback_bindings;
    auto graph = std::make_shared<Graph>(cfg, pre_modules, callback_bindings);

    BMFLOG(BMF_INFO) << "[TEST] Graph created successfully";

    // 1. Dynamically add a decoder node
    NodeConfig decoder_node;
    decoder_node.id = 2;
    decoder_node.alias = "decoder1";
    decoder_node.action = "add";
    decoder_node.module.module_name = "c_ffmpeg_decoder";
    decoder_node.module.module_type = "c++";
    decoder_node.option.json_value_ = {{"input_path", "../../files/big_bunny_10s_30fps.mp4"}};

    StreamConfig out1, out2;
    out1.set_identifier("ffmpeg_decoder_1_1");
    out2.set_identifier("ffmpeg_decoder_1_2");
    decoder_node.output_streams = {out1, out2};
    decoder_node.input_manager = "immediate";
    decoder_node.scheduler = 0;

    GraphConfig add_cfg;
    add_cfg.nodes.push_back(decoder_node);

    BMFLOG(BMF_INFO) << "[DEBUG] Adding decoder node JSON:\n" 
                      << decoder_node.to_json().dump(4);

    int ret = graph->dynamic_add_node(add_cfg);
    ASSERT_EQ(ret, 0);
    BMFLOG(BMF_INFO) << "[TEST] dynamic_add_node decoder1 ret = " << ret;

    // 2. Dynamically add an encoder node
    NodeConfig encoder_node;
    encoder_node.id = 3;
    encoder_node.alias = "encoder1";
    encoder_node.action = "add";
    encoder_node.module.module_name = "c_ffmpeg_encoder";
    encoder_node.module.module_type = "c++";

    encoder_node.option.json_value_ = {
        {"output_path", "out1.mp4"},
        {"video_params", {{"codec","h264"}, {"width",320}, {"height",240}, {"crf",23}, {"preset","veryfast"}}},
        {"audio_params", {{"channels",2}, {"bit_rate",131000}, {"codec","aac"}, {"sample_rate",44100}}}
    };

    StreamConfig in1, in2;
    in1.set_identifier("ffmpeg_decoder_1_1");
    in2.set_identifier("ffmpeg_decoder_1_2");
    encoder_node.input_streams = {in1, in2};
    encoder_node.input_manager = "immediate";
    encoder_node.scheduler = 1;

    GraphConfig add_encoder_cfg;
    add_encoder_cfg.nodes.push_back(encoder_node);

    BMFLOG(BMF_INFO) << "[DEBUG] Adding encoder node JSON:\n" 
                      << encoder_node.to_json().dump(4);

    ret = graph->dynamic_add_node(add_encoder_cfg);
    ASSERT_EQ(ret, 0);
    BMFLOG(BMF_INFO) << "[TEST] dynamic_add_node encoder1 ret = " << ret;

    // 3. Dynamically reset decoder1 node
    NodeConfig reset_node;
    reset_node.id = 2;
    reset_node.alias = "decoder1";
    reset_node.action = "reset";
    reset_node.module.module_name = "c_ffmpeg_decoder";
    reset_node.module.module_type = "c++";
    reset_node.option.json_value_ = json::object();

    GraphConfig reset_cfg;
    reset_cfg.nodes.push_back(reset_node);

    BMFLOG(BMF_INFO) << "[DEBUG] Reset node JSON:\n" 
                      << reset_node.to_json().dump(4);

    ret = graph->dynamic_reset_node(reset_cfg);
    ASSERT_EQ(ret, 0);
    BMFLOG(BMF_INFO) << "[TEST] dynamic_reset_node decoder1 ret = " << ret;

    // 4. Dynamically remove decoder1 node
    NodeConfig remove_node;
    remove_node.id = 2;
    remove_node.alias = "decoder1";
    remove_node.action = "remove";

    GraphConfig remove_cfg;
    remove_cfg.nodes.push_back(remove_node);

    BMFLOG(BMF_INFO) << "[DEBUG] Remove node JSON:\n" 
                      << remove_node.to_json().dump(4);

    ret = graph->dynamic_remove_node(remove_cfg);
    ASSERT_EQ(ret, 0);
    BMFLOG(BMF_INFO) << "[TEST] dynamic_remove_node decoder1 ret = " << ret;

    // 5. Dynamically remove encoder1 node
    NodeConfig remove_encoder;
    remove_encoder.id = 3;
    remove_encoder.alias = "encoder1";
    remove_encoder.action = "remove";

    GraphConfig remove_encoder_cfg;
    remove_encoder_cfg.nodes.push_back(remove_encoder);

    BMFLOG(BMF_INFO) << "[DEBUG] Remove encoder node JSON:\n" 
                      << remove_encoder.to_json().dump(4);

    ret = graph->dynamic_remove_node(remove_encoder_cfg);
    ASSERT_EQ(ret, 0);
    BMFLOG(BMF_INFO) << "[TEST] dynamic_remove_node encoder1 ret = " << ret;
}
