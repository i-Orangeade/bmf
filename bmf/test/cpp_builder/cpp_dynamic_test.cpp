#include "builder.hpp"
#include <fstream>
#include <unistd.h>
#include "nlohmann/json.hpp"
#include "cpp_test_helper.h"

namespace {

bmf_sdk::JsonParam Json(const nlohmann::json &value) {
    return bmf_sdk::JsonParam(value);
}

} // namespace

TEST(cpp_dynamic_config, add_source_with_output_links) {
    auto update_graph = bmf::builder::Graph(bmf::builder::NormalMode);
    auto decoder = update_graph.Decode(
        Json({{"input_path", "input.mp4"}}), "decoder1");

    update_graph.DynamicAdd(
        decoder, bmf_sdk::JsonParam(),
        Json({{"alias", "pass_through"}, {"streams", 2}}));

    auto config = nlohmann::json::parse(update_graph.Dump());
    ASSERT_EQ(config["nodes"].size(), 1);
    const auto &node = config["nodes"][0];
    EXPECT_EQ(node["action"], "add");
    ASSERT_EQ(node["output_streams"].size(), 4);
    EXPECT_EQ(
        node["output_streams"][2]["identifier"].get<std::string>().find(
            "pass_through."),
        0);
    EXPECT_EQ(
        node["output_streams"][3]["identifier"].get<std::string>().find(
            "pass_through."),
        0);
}

TEST(cpp_dynamic_config, add_sink_with_input_links) {
    auto update_graph = bmf::builder::Graph(bmf::builder::NormalMode);
    auto encoder = update_graph.Encode(
        Json({{"output_path", "output.mp4"}}), "encoder1");

    update_graph.DynamicAdd(
        encoder,
        Json({{"alias", "pass_through"}, {"streams", 2}}));

    auto config = nlohmann::json::parse(update_graph.Dump());
    ASSERT_EQ(config["nodes"].size(), 1);
    const auto &node = config["nodes"][0];
    EXPECT_EQ(node["action"], "add");
    ASSERT_EQ(node["input_streams"].size(), 2);
    EXPECT_EQ(
        node["input_streams"][0]["identifier"].get<std::string>().find(
            "pass_through."),
        0);
    EXPECT_EQ(
        node["input_streams"][1]["identifier"].get<std::string>().find(
            "pass_through."),
        0);
}

TEST(cpp_dynamic_config, remove_node) {
    auto update_graph = bmf::builder::Graph(bmf::builder::NormalMode);
    update_graph.DynamicRemove(Json({{"alias", "decoder1"}}));

    auto config = nlohmann::json::parse(update_graph.Dump());
    ASSERT_EQ(config["nodes"].size(), 1);
    EXPECT_EQ(config["nodes"][0]["alias"], "decoder1");
    EXPECT_EQ(config["nodes"][0]["action"], "remove");
}

TEST(cpp_dynamic_config, reset_node) {
    auto update_graph = bmf::builder::Graph(bmf::builder::NormalMode);
    update_graph.DynamicReset(
        Json({{"alias", "encoder1"}, {"video_params", {{"crf", 23}}}}));

    auto config = nlohmann::json::parse(update_graph.Dump());
    ASSERT_EQ(config["nodes"].size(), 1);
    EXPECT_EQ(config["nodes"][0]["alias"], "encoder1");
    EXPECT_EQ(config["nodes"][0]["action"], "reset");
    EXPECT_EQ(config["nodes"][0]["option"]["video_params"]["crf"], 23);
}

TEST(cpp_dynamic_config, rejects_invalid_arguments) {
    auto update_graph = bmf::builder::Graph(bmf::builder::NormalMode);
    EXPECT_THROW(update_graph.DynamicRemove(Json(nlohmann::json::object())),
                 std::logic_error);
    EXPECT_THROW(update_graph.DynamicReset(Json({{"alias", ""}})),
                 std::logic_error);

    auto decoder = update_graph.Decode(
        Json({{"input_path", "input.mp4"}}), "decoder1");
    EXPECT_THROW(
        update_graph.DynamicAdd(
            decoder, bmf_sdk::JsonParam(),
            Json({{"alias", "pass_through"}, {"streams", -1}})),
        std::logic_error);
}

TEST(cpp_dynamic_config, rejects_update_before_start) {
    auto main_graph = bmf::builder::Graph(bmf::builder::NormalMode);
    auto update_graph = bmf::builder::Graph(bmf::builder::NormalMode);
    update_graph.DynamicRemove(Json({{"alias", "decoder1"}}));

    EXPECT_THROW(main_graph.Update(update_graph), std::logic_error);
}

TEST(cpp_dynamic_graph, add_and_remove_source_node) {
    const std::string input_file =
        "../../files/big_bunny_10s_30fps.mp4";
    const std::string python_module_dir =
        "../../../bmf/test/dynamical_graph";

    auto main_graph = bmf::builder::Graph(bmf::builder::NormalMode);
    auto decoder = main_graph.Decode(
        Json({{"input_path", input_file}}), "decoder0");
    auto pass_through = main_graph.Module(
        {decoder["video"], decoder["audio"]}, "reset_pass_through",
        bmf::builder::ModuleType::Python,
        Json(nlohmann::json::object()), "pass_through", python_module_dir, "",
        bmf::builder::InputManagerType::Immediate, 0);

    main_graph.Start(false, true);
    usleep(20000);

    auto add_graph = bmf::builder::Graph(bmf::builder::NormalMode);
    auto added_decoder = add_graph.Decode(
        Json({{"input_path", input_file}}), "decoder1");
    add_graph.DynamicAdd(
        added_decoder, bmf_sdk::JsonParam(),
        Json({{"alias", "pass_through"}, {"streams", 2}}));
    EXPECT_EQ(main_graph.Update(add_graph), 0);
    usleep(30000);

    auto remove_graph = bmf::builder::Graph(bmf::builder::NormalMode);
    remove_graph.DynamicRemove(Json({{"alias", "decoder1"}}));
    EXPECT_EQ(main_graph.Update(remove_graph), 0);

    auto missing_graph = bmf::builder::Graph(bmf::builder::NormalMode);
    missing_graph.DynamicRemove(Json({{"alias", "missing_decoder"}}));
    EXPECT_EQ(main_graph.Update(missing_graph), -1);

    sleep(1);
    EXPECT_EQ(main_graph.ForceClose(), 0);
}

// Dynamic reset function test
TEST(cpp_dynamic_reset, reset_pass_through_node) {
    const std::string reset_marker_file = "./cpp_dynamic_reset.marker";
    const std::string input_file = "../../files/big_bunny_10s_30fps.mp4";
    BMF_CPP_FILE_REMOVE(reset_marker_file);

    // 1. Create main graph
    auto main_graph = bmf::builder::Graph(bmf::builder::NormalMode);
    BMFLOG(BMF_INFO) << "Main graph created.";

    // 2. Add decoder node
    nlohmann::json decode_para = {
        {"input_path", input_file},
        {"alias", "decoder0"}
    };
    auto decoder_node = main_graph.Decode(bmf_sdk::JsonParam(decode_para));
    auto video_stream = decoder_node["video"];
    auto audio_stream = decoder_node["audio"];
    BMFLOG(BMF_INFO) << "Decoder node created successfully.";

    // 3. Add PassThrough node to be reset
    std::vector<bmf::builder::Stream> pass_through_inputs = {video_stream, audio_stream};
    nlohmann::json pass_through_para = {};
    bmf_sdk::JsonParam pass_through_option(nlohmann::json::object());
    const std::string python_module_dir = "../../../bmf/test/dynamical_graph";
    auto pass_through_node = main_graph.Module(
        pass_through_inputs,
        "reset_pass_through",
        bmf::builder::ModuleType::Python,
        pass_through_option,
        "reset_pass_through",
        python_module_dir,
        "",
        bmf::builder::InputManagerType::Immediate,
        0
    );
    BMFLOG(BMF_INFO) << "PassThrough node created successfully.";

    // 4. Non-blocking start graph
    main_graph.Start(true, true);
    BMFLOG(BMF_INFO) << "Waiting 20ms to ensure node initialization";
    usleep(20000);

    // 5. Construct dynamic reset configuration
    nlohmann::json reset_config = {
        {"alias", "reset_pass_through"},
        {"reset_marker_path", reset_marker_file},
        {"video_params", {
            {"codec", "h264"},
            {"width", 320},
            {"height", 240},
            {"crf", 23},
            {"preset", "veryfast"}
        }}
    };
    bmf_sdk::JsonParam reset_config_param(reset_config);
    BMFLOG(BMF_INFO) << "Dynamic reset configuration:\n" << reset_config.dump(2);

    // 6. Create empty reset graph
    auto temp_graph = bmf::builder::Graph(bmf::builder::NormalMode);

    // 7. Temporary graph describes reset information
    temp_graph.DynamicReset(reset_config_param);

    // 8. Main graph performs update
    int update_ret = main_graph.Update(temp_graph);
    if (update_ret != 0) {
        BMFLOG(BMF_ERROR) << "Dynamic reset call failed, return code: " << update_ret;
        FAIL() << "Dynamic reset node call failed.";
    }
    BMFLOG(BMF_INFO) << "Waiting 1 second to ensure processing is complete.";
    sleep(1);

    // 9. Close graph
    int close_ret = main_graph.Close();
    if (close_ret != 0) {
        BMFLOG(BMF_ERROR) << "Graph close failed, return code: " << close_ret;
        FAIL() << "Graph close failed";
    }
    EXPECT_TRUE(std::ifstream(reset_marker_file).good())
        << "The module dynamic_reset callback was not invoked.";
    BMF_CPP_FILE_REMOVE(reset_marker_file);
    BMFLOG(BMF_INFO) << "Main graph closed successfully.";
}
