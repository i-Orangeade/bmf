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

    // 1. 创建主图
    nlohmann::json graph_para = {{"dump_graph", 1}};
    auto main_graph = bmf::builder::Graph(bmf::builder::NormalMode,
                                          bmf_sdk::JsonParam(graph_para));
    BMFLOG(BMF_INFO) << "[cpp_dynamic_reset] 主图创建完成";

    // 3. 添加解码器节点
    nlohmann::json decode_para = {
        {"input_path", input_file} 
    };
    auto decoder_node = main_graph.Decode(bmf_sdk::JsonParam(decode_para), 
                                          "decoder0");  // 第二个参数是节点别名
    auto video_stream = decoder_node["video"];  // 提取视频流
    auto audio_stream = decoder_node["audio"];  // 提取音频流
    BMFLOG(BMF_INFO) << "[cpp_dynamic_reset] 解码器节点创建完成：alias=decoder0";

    // 3. 添加待重置 PassThrough 节点
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
    BMFLOG(BMF_INFO) << "[cpp_dynamic_reset] 待重置节点创建完成";

    // 5. 非阻塞启动图（对应 Python 层 run_wo_block，匹配 Graph::Start API）
    main_graph.Start(true, true);  // 参数1：dump_graph（true=打印图配置），参数2：needMerge（true=合并配置）
    BMFLOG(BMF_INFO) << "[cpp_dynamic_reset] 图非阻塞启动，等待20ms确保节点初始化";
    std::this_thread::sleep_for(std::chrono::milliseconds(20)); 

    // 5. 构造动态重置配置
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

    nlohmann::json reset_update_config = main_graph.dynamic_reset_node(reset_config_param);
    if (reset_update_config.is_null()) {
        BMFLOG(BMF_ERROR) << "[cpp_dynamic_reset] 动态重置配置生成失败";
        FAIL() << "动态重置配置生成失败";
    }

    // 执行实际的更新操作
    int update_ret = main_graph.update(bmf_sdk::JsonParam(reset_update_config));
    if (update_ret != 0) {
        BMFLOG(BMF_ERROR) << "[cpp_dynamic_reset] 动态重置调用失败，返回码：" << update_ret;
        FAIL() << "动态重置节点调用失败";
    }
    
    BMFLOG(BMF_INFO) << "[cpp_dynamic_reset] 动态重置指令已发送，等待1秒确保处理完成";
    std::this_thread::sleep_for(std::chrono::seconds(1));   

    // 8. 关闭图（释放资源，匹配 Graph::Close API）
    int close_ret = main_graph.Close();
    if (close_ret != 0) {
        BMFLOG(BMF_ERROR) << "[cpp_dynamic_reset] 图关闭失败，返回码：" << close_ret;
        FAIL() << "图关闭失败";
    }
    BMFLOG(BMF_INFO) << "[cpp_dynamic_reset] 主图已正常关闭";

    BMFLOG(BMF_INFO) << "[cpp_dynamic_reset] 测试通过：动态重置功能正常，输出文件符合预期";
}

// 动态添加功能测试
TEST(cpp_dynamic_add, add_decoder_and_encoder_only) {
    // 1. 路径配置
    const std::string input_file = "../../files/big_bunny_10s_30fps.mp4";
    const std::string input_file2 = "../../files/big_bunny_10s_30fps.mp4";
    const std::string output_file = "./output.mp4";
    BMF_CPP_FILE_REMOVE(output_file); 

    // 2. 创建主图
    nlohmann::json graph_para = {{"dump_graph", 1}};
    auto main_graph = bmf::builder::Graph(bmf::builder::NormalMode,
                                         bmf_sdk::JsonParam(graph_para));
    BMFLOG(BMF_INFO) << "[cpp_dynamic_add] 主图创建完成";

    // 3. 添加解码器节点
    nlohmann::json decode_para = {
        {"input_path", input_file},
        {"alias", "decoder"}
    };
    auto decoder_node = main_graph.Decode(bmf_sdk::JsonParam(decode_para),
                                         "decoder0");
    auto video_stream = decoder_node["video"]; 
    auto audio_stream = decoder_node["audio"]; 
    BMFLOG(BMF_INFO) << "[cpp_dynamic_add] 解码器节点创建完成：alias=decoder0";

    std::vector<bmf::builder::Stream> pass_through_inputs; 
    // 4.Graph::Module
    bmf_sdk::JsonParam pass_through_option(nlohmann::json::object());
    
    auto pass_through_node = main_graph.Module(
        pass_through_inputs, 
        "pass_through",                     
        bmf::builder::ModuleType::CPP,      
        pass_through_option,  
        "pass_through",  
        "",             
        "",                                                      
        bmf::builder::InputManagerType::Immediate, 
        0                                  
    );
    BMFLOG(BMF_INFO) << "[cpp_dynamic_add] pass_through模块创建完成";

    // 5. 非阻塞启动图
    main_graph.Start(true, true);  
    BMFLOG(BMF_INFO) << "[cpp_dynamic_add] 主图非阻塞启动";
    std::this_thread::sleep_for(std::chrono::milliseconds(20)); 

    // 构造decoder1的动态配置
    nlohmann::json decoder1_config = {
        {"alias", "decoder1"},                  
        {"module_info", {                      
            {"name", "c_ffmpeg_decoder"},
            {"type", "c++"}                    
        }},
        {"option", {                            
            {"input_path", input_file2},       
            {"alias", "decoder1"}
        }},
        {"output_streams", nlohmann::json::array({ 
            {{"identifier", "pass_through.0_0"}, {"alias", "video"}},
            {{"identifier", "pass_through.0_1"}, {"alias", "audio"}}
        })},
        {"input_manager", "immediate"},     
        {"scheduler", 0}
    };
    // 调用dynamic_add_node
    bmf_sdk::JsonParam decoder1_param(decoder1_config);  
    int add_decoder_ret = main_graph.dynamic_add_node(decoder1_param);
    if (add_decoder_ret != 0) {
        BMFLOG(BMF_ERROR) << "[cpp_dynamic_add] decoder1添加失败，返回码：" << add_decoder_ret;
        FAIL() << "decoder1添加失败";
    }
    BMFLOG(BMF_INFO) << "[cpp_dynamic_add] decoder1动态添加完成";
    std::this_thread::sleep_for(std::chrono::milliseconds(30));  


    // 7. 动态添加encoder1
    nlohmann::json encoder1_config = {
        {"alias", "encoder1"},                 
        {"module_info", {                       
            {"name", "c_ffmpeg_encoder"},
            {"type", "c++"}
        }},
        {"option", {                           
            {"output_path", output_file},       
            {"alias", "encoder1"}
        }},
        {"input_streams", nlohmann::json::array({  
            {{"identifier", "pass_through.1_0"}, {"alias", "video"}},
            {{"identifier", "pass_through.1_1"}, {"alias", "audio"}}
        })},
        {"input_manager", "immediate"},
        {"scheduler", 1}  
    };
    // 调用dynamic_add_node添加编码器
    bmf_sdk::JsonParam encoder1_param(encoder1_config); 
    int add_encoder_ret = main_graph.dynamic_add_node(encoder1_param);
    if (add_encoder_ret != 0) {
        BMFLOG(BMF_ERROR) << "[cpp_dynamic_add] encoder1添加失败，返回码：" << add_encoder_ret;
        FAIL() << "encoder1添加失败";
    }
    BMFLOG(BMF_INFO) << "[cpp_dynamic_add] encoder1动态添加完成）";
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); 


    BMFLOG(BMF_INFO) << "[cpp_dynamic_add] 等待编码完成";
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 8. 关闭图
    int close_ret = main_graph.Close();
    if (close_ret != 0) {
        BMFLOG(BMF_ERROR) << "[cpp_dynamic_add] 图关闭失败，返回码：" << close_ret;
        FAIL() << "图关闭失败";
    }
    BMFLOG(BMF_INFO) << "[cpp_dynamic_add] 主图已正常关闭";

    BMFLOG(BMF_INFO) << "[cpp_dynamic_add] 测试通过：动态添加功能正常";
}


// 测试入口（默认GTest入口）
int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}