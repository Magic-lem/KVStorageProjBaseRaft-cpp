//
// RPC配置类的实现，用于加载和查询配置文件中的配置项信息
// created by magic_pri on 2024-7-5
//

#include "mprpcconfig.h"
#include <iostream>

/*
LoadConfigFile 函数
功能：从指定的配置文件中加载配置项到 m_configMap 中。
参数：config_file 是要加载的配置文件的路径。
*/
void MprpcConfig::LoadConfigFile(const char *config_file) {
  FILE *pf = fopen(config_file, "r");   // 通过标准库函数打开文件
  if (pf == nullptr) {  // 打开文件失败
    std::cout << config_file << " is not exist!" << std::endl;
    exit(EXIT_FAILURE);
  }

  // 循环按行读取文件内容
  while (!feof(pf)) {   // feof判断是否读完
    char buf[512] = {0};
    fgets(buf, 512, pf);    // 读取一行内容（最多511个字符 + '\0'）

    // 去除字符串前面多余的空格
    std::string read_buf(buf);
    Trim(read_buf);

    // 如果是注释或者空行，则跳过
    if (read_buf[0] == '#' || read_buf.empty()) {
      continue;
    }

    // 解析配置项
    int idx = read_buf.find('=');
    if (idx == -1) { // 配置项不合法
      continue;
    }

    // 提取配置项：key和value
    std::string key;
    std::string value;
    key = read_buf.substr(0, idx);   // 键
    Trim(key);

    int endidx = read_buf.find('\n', idx);  
    value = read_buf.substr(idx + 1, endidx - idx - 1); // 值
    Trim(value);

    // 将配置项插入到哈希表
    m_configMap.insert({key, value});
  }
  fclose(pf);   // 关闭文件，释放资源
}

/*
Load 函数
功能：根据指定的键 key 查询配置项的值。
参数：key 是要查询的配置项的键名。
*/
std::string MprpcConfig::Load(const std::string &key) {
  auto it = m_configMap.find(key);
  if (it == m_configMap.end()) {    // 没找到
    return "";
  }
  return it->second;
}

/*
Trim 函数
功能：去除字符串 src_buf 前后的空格。
参数：src_buf 是要处理的字符串的引用。
*/
void MprpcConfig::Trim(std::string &src_buf) {
  int idx = src_buf.find_first_not_of(' ');     // 查询第一个不是空格的位置
  if (idx != -1) {  // 说明前面存在空格
    src_buf = src_buf.substr(idx, src_buf.size() - idx);
  }

  // 去除末尾的空格
  idx = src_buf.find_last_not_of(' ');
  if (idx != -1) {    // 说明字符串后面有空格
    src_buf = src_buf.substr(0, idx + 1);
  }
}