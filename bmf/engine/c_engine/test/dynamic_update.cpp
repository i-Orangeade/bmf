#include <fstream>
#include <chrono>
#include <thread>
#include "gtest/gtest.h"
#include <bmf/sdk/log.h>
#include <bmf/sdk/json_param.h>
#include "../include/common.h"
#include "../../connector/include/builder.hpp"

using namespace bmf::builder;
using namespace bmf_sdk;
using json = nlohmann::json;
using JsonParam = bmf_sdk::JsonParam;

TEST(builder, dynamic_interfaces) {
    BMFLOG_SET_LEVEL(BMF_INFO);
    BMFLOG(BMF_INFO) << "===== 动态接口测试 =====";
    int ret = 0;
    const int filter_node_id = 2;  // 定义滤镜节点的固定 ID（2），确保动态操作的是同一个节点。

    // 1. 初始化Graph
    auto graph = bmf::builder::Graph(bmf::builder::NormalMode, bmf_sdk::JsonParam());
    ret = graph.Run(false, false);  //调用Run方法初始化 Graph
    ASSERT_EQ(ret, 0) << "Graph init failed (ret=" << ret << ")";

    // 2. 创建解码节点
    nlohmann::json decoder_para;
    decoder_para["input_path"] = "../../files/big_bunny_10s_30fps.mp4";
    auto decoder = graph.Decode(bmf_sdk::JsonParam(decoder_para), "decoder_node");
    std::string video_stream_id = decoder.Stream(0).GetName();  //获取解码节点输出的第 0 路流（视频流）的唯一标识（identifier）。
    BMFLOG(BMF_INFO) << "Video stream identifier: " << video_stream_id;     // 后续添加滤镜节点时，需通过该标识关联输入流。若流索引错误（如Stream(1)可能是音频流），会导致滤镜节点输入错误。

    // 3. 动态添加滤镜节点
    nlohmann::json add_node_para;
    add_node_para["id"] = filter_node_id;
    add_node_para["input_streams"] = json::array({  // 输入流配置（数组）
        {
            {"identifier", video_stream_id},  // 关联解码节点的视频流
            {"notify", ""} 
        }
    });
    add_node_para["output_streams"] = json::array({
        {
            {"identifier", "c_ffmpeg_filter_2_0"},
            {"notify", ""}
        }
    });
    add_node_para["option"] = json({
        {"filter_desc", "scale=320:240"},
        {"input_pix_fmt", "yuv420p"},
        {"output_pix_fmt", "yuv420p"},
        {"time_base", "1/30"}
    });
    ret = graph.dynamic_add_node(bmf_sdk::JsonParam(add_node_para));
    ASSERT_EQ(ret, 0) << "dynamic_add_node failed (ret=" << ret << ")";
    BMFLOG(BMF_INFO) << "dynamic_add_node success";

    // 4. 动态重置滤镜节点
    nlohmann::json reset_node_para;
    reset_node_para["id"] = filter_node_id;
    reset_node_para["option"] = json({{"filter_desc", "scale=640:480"}});  // 重置参数，实现动态调整滤镜效果 
    ret = graph.dynamic_reset_node(bmf_sdk::JsonParam(reset_node_para));
    ASSERT_EQ(ret, 0) << "dynamic_reset_node failed (ret=" << ret << ")";
    BMFLOG(BMF_INFO) << "dynamic_reset_node success";

    // 5. 创建编码器并执行
    nlohmann::json encoder_para;
    encoder_para["output_path"] = "./core_dynamic_test.mp4";
    encoder_para["video_params"] = json({
        {"codec", "h264"},
        {"width", 640},
        {"height", 480},
        {"crf", 23},
        {"preset", "veryfast"}
    });
    encoder_para["audio_params"] = json({
        {"codec", "aac"},
        {"bit_rate", 128000},
        {"sample_rate", 44100},
        {"channels", 2}
    });
    auto filter_stream = graph.NewPlaceholderStream(); // 创建占位流（关联滤镜输出）
    auto encoder = graph.Encode(filter_stream, decoder.Stream(1), 
                                bmf_sdk::JsonParam(encoder_para), "encoder_node");
    
    ret = graph.Run(false, true);
    ASSERT_EQ(ret, 0) << "Graph merge config failed (ret=" << ret << ")";
    graph.Start();
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 6. 动态删除滤镜节点
    nlohmann::json remove_node_para;
    remove_node_para["id"] = filter_node_id; // 通过 ID 定位要删除的节点
    ret = graph.dynamic_remove_node(bmf_sdk::JsonParam(remove_node_para)); // 调用动态删除接口
    ASSERT_EQ(ret, 0) << "dynamic_remove_node failed (ret=" << ret << ")";
    BMFLOG(BMF_INFO) << "dynamic_remove_node success";

    // 7. 关闭Graph
    ret = graph.Close();
    ASSERT_EQ(ret, 0) << "Graph close failed (ret=" << ret << ")";
    BMFLOG(BMF_INFO) << "===== 接口测试完成 =====";
}