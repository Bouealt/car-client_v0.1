#include <iostream>
#include <fstream>
#include <vector>
#include <boost/asio.hpp>
#include <filesystem>
#include <thread>
#include <chrono>
#include <mutex>
#include <openssl/md5.h>

namespace fs = std::filesystem; // 使用C++17的文件系统库
using boost::asio::ip::tcp; // 使用Boost.Asio的TCP协议 
using namespace std::chrono_literals; // 使用chrono库的字面量

const size_t CHUNK_SIZE = 4096; // 每个数据块的大小
const int MAX_RETRIES = 3; // 最大重连次数
const int RETRY_INTERVAL = 5; // 重试间隔时间，单位为秒

std::mutex mtx; // 互斥锁，用于多线程输出同步

// 计算文件的MD5哈希值
std::string calculate_md5(const fs::path& file_path) {
    std::ifstream file(file_path, std::ios::binary); // 以二进制方式打开文件
    if (!file) {
        throw std::runtime_error("Failed to open file: " + file_path.string()); // 打开文件失败时抛出异常
    }

    MD5_CTX md5Context; // 定义MD5上下文
    MD5_Init(&md5Context); // 初始化MD5上下文

    std::vector<char> buffer(CHUNK_SIZE); // 定义缓冲区

    while (file.read(buffer.data(), buffer.size())) { // 读取文件内容并更新MD5上下文
        MD5_Update(&md5Context, buffer.data(), buffer.size());
    }
    MD5_Update(&md5Context, buffer.data(), file.gcount()); // 处理最后一块数据

    unsigned char digest[MD5_DIGEST_LENGTH]; // 存储MD5结果
    MD5_Final(digest, &md5Context); // 计算MD5

    std::string result;
    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", digest[i]); // 将MD5结果转换为字符串
        result.append(buf);
    }
    return result; // 返回MD5字符串
}

// 发送文件
bool send_file(tcp::socket& socket, const fs::path& file_path) {
    std::ifstream file(file_path, std::ios::binary); // 以二进制方式打开文件
    if (!file) {
        std::lock_guard<std::mutex> lock(mtx); // 使用锁保护输出
        std::cerr << "Failed to open file: " << file_path << std::endl;
        return false;
    }

    file.seekg(0, std::ios::end); // 移动文件指针到文件末尾
    size_t file_size = file.tellg(); // 获取文件大小
    file.seekg(0, std::ios::beg); // 移动文件指针到文件开头

    std::string file_name = file_path.string(); // 获取文件名
    uint32_t name_length = file_name.size(); // 文件名长度
    uint32_t net_name_length = htonl(name_length); // 将长度转换为网络字节序
    boost::asio::write(socket, boost::asio::buffer(&net_name_length, sizeof(net_name_length))); // 发送文件名长度
    boost::asio::write(socket, boost::asio::buffer(file_name.data(), name_length)); // 发送文件名

    uint32_t net_file_size = htonl(file_size); // 将文件大小转换为网络字节序
    boost::asio::write(socket, boost::asio::buffer(&net_file_size, sizeof(net_file_size))); // 发送文件大小

    std::vector<char> buffer(CHUNK_SIZE); // 定义缓冲区
    size_t total_bytes_sent = 0;
    while (total_bytes_sent < file_size) {
        file.read(buffer.data(), CHUNK_SIZE); // 读取文件内容到缓冲区
        std::streamsize bytes_read = file.gcount(); // 获取读取的字节数
        if (bytes_read > 0) {
            try {
                boost::asio::write(socket, boost::asio::buffer(buffer.data(), bytes_read)); // 发送缓冲区内容
                total_bytes_sent += bytes_read; // 更新已发送字节数

                std::lock_guard<std::mutex> lock(mtx); // 使用锁保护输出
                std::cout << "\rProgress: " << (100 * total_bytes_sent / file_size) << "%" << std::flush; // 显示进度
            } catch (const boost::system::system_error& e) {
                std::lock_guard<std::mutex> lock(mtx);
                std::cerr << "Failed to send data: " << e.what() << std::endl;
                return false;
            }
        }
    }
    std::cout << std::endl;

    std::string md5_hash = calculate_md5(file_path); // 计算文件的MD5哈希值
    std::cout << "Calculated MD5: " << md5_hash << std::endl;

    uint32_t hash_length = md5_hash.size(); // MD5哈希值长度
    uint32_t net_hash_length = htonl(hash_length); // 将长度转换为网络字节序
    boost::asio::write(socket, boost::asio::buffer(&net_hash_length, sizeof(net_hash_length))); // 发送MD5哈希值长度
    boost::asio::write(socket, boost::asio::buffer(md5_hash.data(), hash_length)); // 发送MD5哈希值

    std::lock_guard<std::mutex> lock(mtx);
    std::cout << "Sent file: " << file_name << " (" << file_size << " bytes), MD5: " << md5_hash << std::endl;
    return true;
}

// 管理连接，处理重连和超时
void manage_connection(const std::string& server, const std::string& port, const fs::path& file_path) {
    boost::asio::io_service io_service;
    tcp::resolver resolver(io_service);
    tcp::resolver::results_type endpoints = resolver.resolve(server, port); // 域名解析

    int retry_count = 0;
    while (retry_count < MAX_RETRIES) {
        try {
            tcp::socket socket(io_service);
            boost::asio::connect(socket, endpoints); // 同步连接

            if (send_file(socket, file_path)) { // 发送文件
                return;
            }
        } catch (const boost::system::system_error& e) {
            std::lock_guard<std::mutex> lock(mtx);
            std::cerr << "Connection error: " << e.what() << " - Retrying (" << ++retry_count << "/" << MAX_RETRIES << ")" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(RETRY_INTERVAL));  // 延迟后重试
        }
    }

    std::lock_guard<std::mutex> lock(mtx);
    std::cerr << "Failed to send file after " << MAX_RETRIES << " retries." << std::endl;
}

int main() {
    std::string server = "tstit.x3322.net"; // 修改为你的服务器域名或IP
    std::string port = "8889";
    fs::path dir_path = "/mnt/hgfs/share/DataSet/B";  // 修改为你的数据集路径

    try {
        for (const auto& entry : fs::recursive_directory_iterator(dir_path)) { // 递归遍历目录
            if (fs::is_regular_file(entry)) { // 只处理文件
                std::cout << "Connecting to server " << server << " on port " << port << std::endl;
                manage_connection(server, port, entry.path()); // 管理连接并发送文件
            }
        }
        std::cout << "All files processed." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}

