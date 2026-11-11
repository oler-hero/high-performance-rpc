
#include "thread_pool_singleton.h"

namespace meeting_ctrl {

/**
 * @brief 线程池单例实例的定义
 * @details 
 * 这里定义了静态成员变量instance_，为其分配内存空间。
 * 默认初始化为nullptr，表示尚未创建线程池实例。
 * 
 * **初始化时机：**
 * - 在程序启动时（main函数执行前）进行零初始化
 * - unique_ptr默认构造为nullptr
 * 
 * **生命周期：**
 * - 从程序开始到程序结束
 * - 程序结束时unique_ptr自动调用ThreadPool的析构函数
 */
std::unique_ptr<ThreadPool> ThreadPoolSingleton::instance_;

/**
 * @brief 保护单例操作的互斥锁定义
 * @details
 * 这个互斥锁保护所有对instance_的操作，确保线程安全。
 * 
 * **作用范围：**
 * - 保护单例的创建过程
 * - 保护单例的销毁过程  
 * - 保护对instance_的所有访问
 * 
 * **注意事项：**
 * - 这个锁只保护单例本身的操作
 * - 不保护ThreadPool内部的操作（ThreadPool有自己的锁机制）
 * - 锁的粒度较粗，但操作频率不高，性能影响可接受
 * 
 * **初始化：**
 * - std::mutex默认构造为未锁定状态
 * - 自动具有正确的初始状态
 */
std::mutex ThreadPoolSingleton::mutex_;

} // namespace meeting_ctrl

/**
 * @note 编程最佳实践
 * 
 * **为什么不在头文件中定义？**
 * 1. 避免重复定义：如果在头文件中定义，每个包含该头文件的源文件都会定义一次
 * 2. 遵循ODR原则：One Definition Rule，每个变量只能定义一次
 * 3. 减少编译依赖：定义放在源文件中，不会影响其他文件的编译
 * 
 * **为什么不使用inline static？**
 * C++17引入了inline static，可以在头文件中定义，但考虑到：
 * 1. 兼容性：不是所有项目都使用C++17
 * 2. 明确性：分离声明和定义更加清晰
 * 3. 传统做法：这是经典的C++单例实现方式
 * 
 * **内存布局：**
 * - 这些静态变量存储在程序的数据段（data segment）
 * - 具有静态存储期（static storage duration）
 * - 在main函数执行前初始化，在程序结束后销毁
 */