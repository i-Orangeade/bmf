#include <fstream>
#include <chrono>
#include <thread>
#include "gtest/gtest.h"
#include <bmf/sdk/log.h>
#include <bmf/sdk/json_param.h>
#include "../include/common.h"
#include "../../connector/include/builder.hpp"
#include "cpp_test_helper.h" 

// 动态重置功能测试
TEST(cpp_dynamic_reset, reset_pass_through_node) {
    const std::string output_file = "./output_reset_cpp.mp4";
    const std::string input_file = "../../files/big_bunny_10s_30fps.mp4";
    BMF_CPP_FILE_REMOVE(output_file); 

    // 2. 创建主图
    nlohmann::json graph_para = {{"dump_graph", 1}};
    auto main_graph = bmf::builder::Graph(bmf::builder::NormalMode,
                                          bmf_sdk::JsonParam(graph_para));
    BMFLOG(BMF_INFO) << "[cpp_dynamic_reset] 主图创建完成（NormalMode）";

    // 3. 添加解码器节点
    nlohmann::json decode_para = {
        {"input_path", input_file} 
    };
    auto decoder_node = main_graph.Decode(bmf_sdk::JsonParam(decode_para), 
                                          "decoder0");  // 第二个参数是节点别名
    auto video_stream = decoder_node["video"];  // 提取视频流
    auto audio_stream = decoder_node["audio"];  // 提取音频流
    BMFLOG(BMF_INFO) << "[cpp_dynamic_reset] 解码器节点创建完成：alias=decoder0";

    // 4. 添加待重置的 PassThrough 节点（关键：匹配 Graph::Module API）
    std::vector<bmf::builder::Stream> pass_through_inputs = {video_stream, audio_stream};
    // 4.2 构造节点参数（仅需别名，其他参数通过 Module 函数参数传递）
    nlohmann::json pass_through_para = {};  // 无额外参数，空JSON即可
    // 4.3 调用 Graph::Module（严格匹配参数顺序和类型）
    auto pass_through_node = main_graph.Module(
        pass_through_inputs, 
        "pass_through",                     
        bmf::builder::ModuleType::CPP,      
        bmf_sdk::JsonParam(pass_through_para),  
        "reset_pass_through",               
        "",                                 
        "",                                 
        bmf::builder::InputManagerType::Immediate, 
        0                                  
    );
    BMFLOG(BMF_INFO) << "[cpp_dynamic_reset] 待重置节点创建完成：alias=reset_pass_through";

    // 5. 非阻塞启动图（对应 Python 层 run_wo_block，匹配 Graph::Start API）
    main_graph.Start(true, true);  // 参数1：dump_graph（true=打印图配置），参数2：needMerge（true=合并配置）
    BMFLOG(BMF_INFO) << "[cpp_dynamic_reset] 图非阻塞启动，等待20ms确保节点初始化";
    std::this_thread::sleep_for(std::chrono::milliseconds(20)); 

    // 6. 构造动态重置配置（严格匹配 dynamic_reset_node 要求：必须包含 alias 定位节点）
    nlohmann::json reset_config = {
        {"alias", "reset_pass_through"}, 
        {"output_path", output_file},     
        {"video_params", {               
            {"codec", "h264"},
            {"width", 320},
            {"height", 240},
            {"crf", 23},
            {"preset", "veryfast"}
        }}
    };
    bmf_sdk::JsonParam reset_config_param(reset_config);
    BMFLOG(BMF_INFO) << "[cpp_dynamic_reset] 动态重置配置:\n" << reset_config.dump(2);

    // 7. 执行动态重置（调用 Graph::dynamic_reset_node API，非阻塞）
    int reset_ret = main_graph.dynamic_reset_node(reset_config_param);
    if (reset_ret != 0) {
        BMFLOG(BMF_ERROR) << "[cpp_dynamic_reset] 动态重置调用失败，返回码：" << reset_ret;
        FAIL() << "动态重置节点调用失败";
    }
    BMFLOG(BMF_INFO) << "[cpp_dynamic_reset] 动态重置指令已发送，等待2秒确保处理完成";
    std::this_thread::sleep_for(std::chrono::seconds(2));  // 等待重置后数据处理

    // 8. 关闭图（释放资源，匹配 Graph::Close API）
    int close_ret = main_graph.Close();
    if (close_ret != 0) {
        BMFLOG(BMF_ERROR) << "[cpp_dynamic_reset] 图关闭失败，返回码：" << close_ret;
        FAIL() << "图关闭失败";
    }
    BMFLOG(BMF_INFO) << "[cpp_dynamic_reset] 主图已正常关闭";

    // 9. 验证输出文件（与Python层校验逻辑对齐）
    BMFLOG(BMF_INFO) << "[cpp_dynamic_reset] 开始验证输出文件：" << output_file;
    BMF_CPP_FILE_CHECK(
        output_file,  // 实际输出文件路径
        "./output_reset_cpp.mp4|240|320|10.0|MOV,MP4,M4A,3GP,3G2,MJ2|192235|240486|h264|{\"fps\": \"30.0662251656\"}"
    );
    BMFLOG(BMF_INFO) << "[cpp_dynamic_reset] 测试通过：动态重置功能正常，输出文件符合预期";
}

// 测试入口（默认GTest入口）
int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}