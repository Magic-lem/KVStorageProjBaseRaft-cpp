//
// 跳表的定义和实现
// created by magic_pri on 2024-7-14
//

#ifndef SKIPLIST_H
#define SKIPLIST_H

#include <string>
#include <fstream>
#include <iostream>

#define STORE_FILE "store/dumpFile"   // 宏定义，表示存储文件的路径

static std::string delimiter = ":";   // 静态字符串，用于分隔键值对的分隔符

/*
Node 类模板
主要功能：表示跳表中的节点，K 是键的类型，V 是值的类型。
*/
template <typename K, typename V>
class Node {
public:
  Node() {}

  Node(K k, V v, int);

  ~Node();

  K get_key() const;

  V get_value() const;

  void set_value(V);

public:
  Node<K, V> **forward;   // 指向不同级别的下一个节点的指针数组（跳表的核心）
  int node_level;   // 节点的级别
  
private:
  K key;
  V value;
};

template <typename K, typename V>
Node<K, V>::Node(const K k, const V v, int level) {   // 构造函数
  this->key = k;
  this->value = v;
  this->node_level = level;

  // 指针数组Node<K, V> *[level + 1]
  this->forward = new Node<K, V> *[level + 1];    // 为什么数组的尺寸是level + 1？  因为在第level层，则从level到0层都存在该节点，所以该节点可以指向level + 1个节点
  memset(this->forward, 0, sizeof(Node<K, V> *) * (level + 1));   // 所有字节填充为0
};

template <typename K, typename V>
Node<K, V>::~Node() {     // 析构函数，释放动态开辟的内存
  delete[] forward;
};

template <typename K, typename V>
K Node<K, V>::get_key() const {
  return key;
};

template <typename K, typename V>
V Node<K, V>::get_value() const {
  return value;
};

template <typename K, typename V>
void Node<K, V>::set_value(V value) {
  this->value = value;
};

/*
SkipListDump 类模板
主要功能：将跳表节点的数据进行序列化和反序列化为字符串，以进行持久化
*/
template <typename K, typename V>
class SkipListDump {
public:
  // 基于boost序列化库
  friend class boost::serialization::access;

  template<class Archive>
  void serialize(Archive &ar, const unsigned int version) {
    ar &keyDumpVt_;
    ar &valDumpVt_;
  }
  
  // 用于存储节点的键和值的向量
  std::vector<K> keyDumpVt_;
  std::vector<V> valDumpVt_;
public:
  void insert(const Node<K, V> &node);
};

/*
SkipListDump::insert 函数
主要功能：将插入节点的key和value保存到数组中
*/
template <typename K, typename V>
void SkipListDump<K, V>::insert(const Node<K, V> &node) {
  keyDumpVt_.emplace_back(node.get_key());
  valDumpVt_.emplace_back(node.get_value());
}

/*
SkipList 跳表类模板
主要功能：提供了跳表类，实现高效的插入、删除、搜索等操作
*/
template <typename K, typename V>
class SkipList {
public:
  SkipList(int);
  ~SkipList();

  int get_random_level();   // 获取一个随机层级，用于确定新节点的层数

  Node<K, V> *create_node(K, V, int);   // 创建一个新节点，包含键、值和层级信息

  int insert_element(K, V);   // 插入一个新的元素到跳表中

  void display_list();    // 显示跳表中的所有元素，主要用于调试

  bool search_element(K, V &value);   // 查找一个指定键值的元素，返回是否找到，并将值赋给传入参数 value。

  void delete_element(K);    //  删除一个指定键的元素

  void insert_set_element(K &, V &);    //  插入或设置元素

  std::string dump_file();      // 将跳表数据导出为字符串，便于持久化存储

  void load_file(const std::string &dumpStr);   // 从字符串加载跳表数据

  void clear(Node<K, V> *);     // 递归删除节点

  int size();     // 返回跳表中元素的数量

private: 
  // 从字符串中解析键值对
  void get_key_value_from_string(const std::string &str, std::string *key, std::string *value);
  // 判断字符串是否有效
  bool is_valid_string(const std::string &str);

private:
  int _max_level;   // 跳表的最大层数
  int _skip_list_level;   // 当前跳表的层数
  Node<K, V> *_header;    // 指向跳表头节点的指针
  std::ofstream _file_writer;   // 文件输出流，用于导出数据
  std::ifstream _file_reader;   // 文件输入流，用于读取数据
  int _element_count;           // 跳表当前的元素数量
  std::mutex _mtx;              // 锁
};


/*
create_node 函数
主要功能：创建一个新的跳表节点，指定键值对和层级
*/
template <typename K, typename V>
Node<K, V> *SkipList<K, V>::create_node(const K k, const V v, int level) {
  Node<K, V> *n = new Node<K, V>(k, v, level);
  return n;
}

/*
insert_element 函数
主要功能：向跳表中插入一个元素（键值对）。它通过随机层级的方式，在跳表中进行插入操作。示例：

                           +------------+
                           |  insert 50 |
                           +------------+
level 4     +-->1+                                                      100
                 |
                 |                      insert +----+
level 3         1+-------->10+---------------> | 50 |          70       100
                                               |    |
                                               |    |
level 2         1          10         30       | 50 |          70       100
                                               |    |
                                               |    |
level 1         1    4     10         30       | 50 |          70       100
                                               |    |
                                               |    |
level 0         1    4   9 10         30   40  | 50 |  60      70       100
                                               +----+

*/
template <typename K, typename V>
int SkipList<K, V>::insert_element(const K key, const V value) {
  _mtx.lock();    // 加锁，保护跳表
  
  // 1. 初始化
  Node<K, V> *current = this->_header;    // current 指针初始化为跳表的头节点 _header
  Node<K, V> *update[_max_level + 1];     // 创建并初始化 update 数组，用于存储每一层需要更新的前驱节点。

  // 2. 查找插入位置：从最高层开始逐层向下
  for (int i = _skip_list_level; i >= 0; i--) {
    while (current->forward[i] != NULL & current->forward[i]->get_key() < key) {    // 从current开始遍历第i层的节点，直到找到第一个比要插入的key更大或相等的，或者找到末尾。（注意，这里current还是上一个，这是由于要把新节点插入到current后面，所以需要保存current）
      current = current->forward[i];    
    }
    update[i] = current;    // 每一层中，新插入的节点的位置就是第一个比他大或相等的节点位置，update保存的为其目标地址的前一个节点
  }
  current = current->forward[0];    // 将current指向节点在原始链表中的目标位置

  // 3. 检查是否存在相同的key
  if (current != NULL && current->get_key() == key) {   // 如果存在相同的key，一定是相等的这一个
    std::cout << "key: " << key << ", exists" << std::endl;
    _mtx.unlock();
    return 1;   // 键已存在，插入失败，返回 1
  }

  // 4. 插入新节点
  if (current == NULL || current->get_key() != key) {
    int random_level = get_random_level();    // 随机获得要插入的层级

    if (random_level > _skip_list_level) {    // 插入层级比当前跳表存在的层级还要大，则调整当前最大层级
      for (int i = _skip_list_level + 1; i < random_level + 1; i++) {
        update[i] = _header;  // 新开一层，这一层就只有这个新节点，则将前一个节点置为头节点
      }
      _skip_list_level = random_level;
    }

    Node<K, V> *inserted_node = create_node(key, value, random_level);

    // 插入节点：一共需要从第0级插入到第random_level级
    for (int i = 0; i <= random_level; i++) {
      inserted_node->forward[i] = update[i]->forward[i];
      update[i]->forward[i] = inserted_node;
    }
    std::cout << "Successfullt inserted key: " << key << ", value: " << value << std::endl;
    _element_count++;
  }

  _mtx.unlock();
  return 0;
}

/*
display_list 函数
主要功能：显示跳表中的所有元素
*/
template<typename K, typename V>
void SkipList<K, V>::display_list() {
  std::cout << "\n*****Skip List*****" << "\n";

  // 逐层打印
  for (int i = 0; i <= _skip_list_level; i++) {
    Node<K, V> *node = this->_header->forward[i];
    std::cout << "Level " << i << ": ";
    while (node != NULL) {
      std::cout << node->get_key() << ":" << node->get_value() << ";";
      node = node->forward[i];
    }
    std::cout << std::endl;
  }
}

/*
dump_file 函数
主要功能：使用 Boost 序列化库，将跳表数据导出为字符串，便于持久化存储
*/
template <typename K, typename V>
std::string SkipList<K, V>::dump_file() {
  Node<K, V> *node = this->_header->forward[0];    // 获取第 0 层（最底层）第一个节点的指针
  SkipListDump<K, V> dumper;    // SkipListDump对象，用于临时存储跳表中的键值对，以便进行序列化
  
  // 遍历所有节点
  while (node != nullptr) {
    dumper.insert(*node);   // 加入到dumper中
    node = node->forward[0];
  }

  // 创建字符串流ss和文本输出档案oa
  std::stringstream ss;
  boost::archive::text_oarchive oa(ss);

  // 序列化 dumper 对象
  oa << dumper;

  return ss.str();
}

/*
load_file 函数
主要功能：从字符串加载跳表数据
*/
template <typename K, typename V>
void SkipList<K, V>::load_file(const std::string &dumpStr) {
  if (dumpStr.empty()) {
    return;
  }
  // 创建 SkipListDump 对象
  SkipListDump<K, V> dumper;
  // 将字符串包装成输入字符串流
  std::stringstream iss(dumpStr);

  // 创建文本输入档案对象并反序列化数据
  boost::archive::text_iarchive ia(iss);
  ia >> dumper;

  // 将数据插入到跳表
  for (int i = 0; i < dumper.keyDumpVt_.size(); ++i) {
    insert_element(dumper.keyDumpVt_[i], dumper.valDumpVt_[i]);
  }
}

/*
size 函数
主要功能：返回跳表中元素的数量
*/
template<typename K, typename V>
int SkipList<K, V>::size() {
  return _element_count;
}

/*
get_key_value_from_string 函数
主要功能：从字符串中解析键值对
*/
template<typename K, typename V> 
void SkipList<K, V>::get_key_value_from_string(const std::string &str, std::string *key, std::string *value) {
  if (!is_valid_string(str)) {    // 字符串不合法
    return;
  }
  *key = str.substr(0, str.find(delimiter));  // 从开头到“:”
  *value = str.substr(str.find(delimiter) + 1, str.length());   // 从“:”到结尾
}

/*
is_valid_string 函数
主要功能：判断字符串是否合法
*/
template<typename K, typename V>
bool SkipList<K, V>::is_valid_string(const std::string &str) {
  if (str.empty()) {
    return false;
  }
  if (str.find(delimiter) == std::string::npos) {
    return false;
  }
  return true;
}

/*
delete_element 函数
主要功能：删除一个指定键的元素
注意：删除本质上和插入一致，只不过是查到了要删除
*/
template<typename K, typename V>
void SkipList<K, V>::delete_element(K key) {
  _mtx.lock();

  // 1. 初始化
  Node<K, V> *current = this->_header;
  Node<K, V> *update[_max_level + 1];
  memset(update, 0, sizeof(Node<K, V> *) * (_max_level + 1));

  // 2. 从高层级逐层查找
  for (int i = _skip_list_level; i >= 0; i--) {
    while (current->forward[i] != NULL && current->forward[i]->get_key() < key) {
      current = current->forward[i];
    }
    update[i] = current;
  } 

  current = current->forward[0];    // 此时，current为目标位置的节点
  // 检查当前节点是否是要删除的节点
  if (current != NULL && current->get_key() == key) { 
    // 是，则从下向上逐层删除
    for (int i = 0; i <= _skip_list_level; i++) {
      if (update[i]->forward[i] != current) {   // 说明当前层级已经没有了要删除的节点
        break;
      }
      update[i]->forward[i] = current->forward[i];    // 删除，则就是把该节点的前面节点指向该节点的下一个节点
    }

    // 如果由于删除导致一些层级没有元素了，则删除该层级
    while (_skip_list_level > 0 && _header->forward[_skip_list_level] == 0) {
      _skip_list_level--;
    }

    std::cout << "Successfully deleted key " << key << std::endl;
    delete current;   // 注意销毁动态内存资源
    _element_count--;
  }
  _mtx.unlock();
  return;
}

/*
insert_set_element 函数
主要功能：插入元素，如果元素存在则改变其值
*/
template <typename K, typename V>
void SkipList<K, V>::insert_set_element(K &key, V &value) {
  V oldValue;
  if (search_element(key, oldValue)) {    // 如果已经有了这个元素
    delete_element(key);    // 删除这个元素
  }
  insert_element(key, value);   // 再把这个新元素插入
}

/*
search_element 函数
主要功能：查找一个指定键值的元素，返回是否找到，并将值赋给传入参数 value。寻找示例：
                           +------------+
                           |  select 60 |
                           +------------+
level 4     +-->1+                                                      100
                 |
                 |
level 3         1+-------->10+------------------>50+           70       100
                                                   |
                                                   |
level 2         1          10         30         50|           70       100
                                                   |
                                                   |
level 1         1    4     10         30         50|           70       100
                                                   |
                                                   |
level 0         1    4   9 10         30   40    50+-->60      70       100
*/
template <typename K, typename V>
bool SkipList<K, V>::search_element(K key, V &value) {
  std::cout << "search_element-----------------" << std::endl;
  Node<K, V> *current = _header;

  // 从最高层级开始向下寻找
  for (int i = _skip_list_level; i >= 0; i--) {
    while (current->forward[i] != NULL && current->forward[i]->get_key() < key) {
      current = current->forward[i];
    }
  }

  current = current->forward[0];    // 目标位置的节点

  // 判断目标位置节点是否是想要的
  if (current and current->get_key() == key) {   
    // 找到了，则将value赋值，并返回成功
    value = current->get_value();
    std::cout << "Found key: " << key << ", value: " << current->get_value() << std::endl;
    return true;
  }

  std::cout << "Not Found Key:" << key << std::endl;
  return false;
}

/*
构造函数
主要功能：初始化跳表
*/
template <typename K, typename V>
SkipList<K, V>::SkipList(int max_level) {
  this->_max_level = max_level;
  this->_skip_list_level = 0;
  this->_element_count = 0;

  // 头节点，key和value都是null
  K k;
  V v;
  this->_header = new Node<K, V>(k, v, _max_level);
}

/*
析构函数
主要功能：释放资源
*/
template <typename K, typename V>
SkipList<K, V>::~SkipList() {
  if (_file_writer.is_open()) {
    _file_writer.close();
  }
  if (_file_reader.is_open()) {
    _file_reader.close();
  }

  // 递归删除跳表节点
  if (_header->forward[0] != nullptr) {
    clear(_header->forward[0]);   // 第0层包含所有节点
  }

  delete (_header);
}

/*
clear 函数
主要功能：递归删除所有节点
*/
template <typename K, typename V>
void SkipList<K, V>::clear(Node<K, V> *cur) {
  if (cur->forward[0] != nullptr) {
    clear(cur->forward[0]);
  }
  delete (cur);
}

/*
get_random_level 函数
主要功能：插入节点时，随机获取节点的最大层级
*/
template <typename K, typename V>
int SkipList<K, V>::get_random_level() {
  int k = 1;
  while (rand() % 2) {
    k++;
  }
  k = (k < _max_level) ? k : _max_level;
  return k;
};

#endif