/*
 * 基于 EventLoop 的线程安全事件队列
 * event_base 由调用模块在构造时传入
 * 跨平台支持：Linux (eventfd) / macOS (pipe)
 */

#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H

#include <queue>
#include <mutex>
#include <memory>
#include <atomic>
#include <functional>
#include <event2/event.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>

// 平台检测
#ifdef __linux__
#include <sys/eventfd.h>
#define USE_EVENTFD 1
#else
#define USE_EVENTFD 0
#endif

namespace EventBase {

/**
 * 基于 event_base 的线程安全事件队列
 * 支持多生产者、单消费者模式
 * 
 * 使用方式：
 *   1. 构造时传入 event_base 和回调函数
 *   2. 生产者调用 Push() 放入数据
 *   3. EventLoop 自动触发回调处理数据
 */
template<typename T>
class EventQueue {
public:
    using EventCallback = std::function<void(T&)>;
    using BatchCallback = std::function<void(std::vector<T>&)>;
    
    /**
     * 构造函数（单项处理模式）
     * @param base 调用模块的 event_base
     * @param callback 每个元素的处理回调
     * @param max_size 队列最大大小，0 表示无限制
     */
    EventQueue(event_base* base, EventCallback callback, size_t max_size = 0)
        : base_(base)
        , max_size_(max_size)
        , stopped_(false)
        , notify_fd_(-1)
        , event_(nullptr)
        , event_callback_(callback)
        , batch_size_(0)
    {
        if (!base_) {
            throw std::invalid_argument("event_base cannot be null");
        }
        if (!callback) {
            throw std::invalid_argument("callback cannot be null");
        }
        
#if !USE_EVENTFD
        pipe_fds_[0] = -1;
        pipe_fds_[1] = -1;
#endif
        
        Initialize();
    }
    
    /**
     * 构造函数（批量处理模式）
     * @param base 调用模块的 event_base
     * @param callback 批量处理回调
     * @param batch_size 每次批量处理的最大数量，0 表示处理全部
     * @param max_size 队列最大大小，0 表示无限制
     */
    EventQueue(event_base* base, BatchCallback callback, size_t batch_size, size_t max_size = 0)
        : base_(base)
        , max_size_(max_size)
        , stopped_(false)
        , notify_fd_(-1)
        , event_(nullptr)
        , batch_callback_(callback)
        , batch_size_(batch_size)
    {
        if (!base_) {
            throw std::invalid_argument("event_base cannot be null");
        }
        if (!callback) {
            throw std::invalid_argument("callback cannot be null");
        }
        
#if !USE_EVENTFD
        pipe_fds_[0] = -1;
        pipe_fds_[1] = -1;
#endif
        
        Initialize();
    }
    
    ~EventQueue() {
        Cleanup();
    }
    
    // 禁止拷贝和移动
    EventQueue(const EventQueue&) = delete;
    EventQueue& operator=(const EventQueue&) = delete;
    EventQueue(EventQueue&&) = delete;
    EventQueue& operator=(EventQueue&&) = delete;
    
    /**
     * 将元素放入队列（非阻塞，移动语义）
     * @return true 成功，false 失败（队列满或已停止）
     */
    bool Push(T&& item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            if (stopped_) {
                return false;
            }
            
            // 检查队列是否已满
            if (max_size_ > 0 && queue_.size() >= max_size_) {
                return false;
            }
            
            queue_.push(std::forward<T>(item));
        }
        
        // 通知 EventLoop
        Notify();
        
        return true;
    }
    
    /**
     * 将元素放入队列（非阻塞，拷贝语义）
     */
    bool Push(const T& item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            if (stopped_) {
                return false;
            }
            
            if (max_size_ > 0 && queue_.size() >= max_size_) {
                return false;
            }
            
            queue_.push(item);
        }
        
        Notify();
        
        return true;
    }
    
    /**
     * 从队列取出元素（非阻塞）
     * @return true 成功，false 失败（队列空）
     */
    bool Pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (queue_.empty()) {
            return false;
        }
        
        item = std::move(queue_.front());
        queue_.pop();
        
        return true;
    }
    
    /**
     * 获取队列当前大小
     */
    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    
    /**
     * 检查队列是否为空
     */
    bool Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
    
    /**
     * 停止队列
     */
    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        
        // 通知可能在等待的消费者
        Notify();
    }
    
    /**
     * 清空队列
     */
    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<T> empty;
        std::swap(queue_, empty);
    }
    
    /**
     * 检查是否已停止
     */
    bool IsStopped() const {
        return stopped_;
    }

private:
    /**
     * 初始化（创建通知机制和 event）
     */
    void Initialize() {
#if USE_EVENTFD
        // Linux: 使用 eventfd
        notify_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (notify_fd_ < 0) {
            throw std::runtime_error("Failed to create eventfd");
        }
#else
        // macOS: 使用 pipe
        if (::pipe(pipe_fds_) != 0) {
            throw std::runtime_error("Failed to create pipe");
        }
        
        // 设置非阻塞
        ::fcntl(pipe_fds_[0], F_SETFL, O_NONBLOCK);
        ::fcntl(pipe_fds_[1], F_SETFL, O_NONBLOCK);
        
        notify_fd_ = pipe_fds_[0];  // 读端用于监听
#endif
        
        // 创建 libevent event
        event_ = event_new(base_, notify_fd_, EV_READ | EV_PERSIST,
            [](evutil_socket_t fd, short events, void* arg) {
                auto* queue = static_cast<EventQueue*>(arg);
                queue->OnEventTriggered();
            }, this);
        
        if (!event_) {
            Cleanup();
            throw std::runtime_error("Failed to create event");
        }
        
        // 添加事件到 EventLoop
        if (event_add(event_, nullptr) < 0) {
            event_free(event_);
            event_ = nullptr;
            Cleanup();
            throw std::runtime_error("Failed to add event");
        }
    }
    
    /**
     * 清理资源
     */
    void Cleanup() {
        if (event_) {
            event_del(event_);
            event_free(event_);
            event_ = nullptr;
        }
        
#if USE_EVENTFD
        if (notify_fd_ >= 0) {
            ::close(notify_fd_);
            notify_fd_ = -1;
        }
#else
        if (pipe_fds_[0] >= 0) {
            ::close(pipe_fds_[0]);
            pipe_fds_[0] = -1;
        }
        if (pipe_fds_[1] >= 0) {
            ::close(pipe_fds_[1]);
            pipe_fds_[1] = -1;
        }
        notify_fd_ = -1;
#endif
    }
    
    /**
     * 通知 EventLoop
     */
    void Notify() {
#if USE_EVENTFD
        // Linux: 写入 eventfd
        uint64_t val = 1;
        ssize_t n = ::write(notify_fd_, &val, sizeof(val));
        (void)n;  // 忽略返回值
#else
        // macOS: 写入 pipe
        char buf = 1;
        ssize_t n = ::write(pipe_fds_[1], &buf, 1);
        (void)n;  // 忽略返回值
#endif
    }
    
    /**
     * 消费通知
     */
    void ConsumeNotify() {
#if USE_EVENTFD
        // Linux: 读取 eventfd
        uint64_t val;
        ::read(notify_fd_, &val, sizeof(val));
#else
        // macOS: 读取 pipe（清空所有数据）
        char buf[256];
        while (::read(notify_fd_, buf, sizeof(buf)) > 0) {
            // 继续读取直到清空
        }
#endif
    }
    
    /**
     * 事件触发回调（在 EventLoop 线程中执行）
     */
    void OnEventTriggered() {
        // 清空通知
        ConsumeNotify();
        
        if (batch_callback_) {
            // 批量处理模式
            ProcessBatch();
        } else if (event_callback_) {
            // 单项处理模式
            ProcessSingle();
        }
    }
    
    /**
     * 单项处理模式
     */
    void ProcessSingle() {
        T item;
        while (Pop(item)) {
            try {
                event_callback_(item);
            } catch (const std::exception& e) {
                std::cerr << "EventQueue: 处理异常: " << e.what() << std::endl;
            }
        }
    }
    
    /**
     * 批量处理模式
     */
    void ProcessBatch() {
        std::vector<T> items;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            if (queue_.empty()) {
                return;
            }
            
            size_t limit = batch_size_ > 0 ? batch_size_ : queue_.size();
            items.reserve(std::min(limit, queue_.size()));
            
            while (!queue_.empty() && (batch_size_ == 0 || items.size() < batch_size_)) {
                items.push_back(std::move(queue_.front()));
                queue_.pop();
            }
        }
        
        if (!items.empty()) {
            try {
                batch_callback_(items);
            } catch (const std::exception& e) {
                std::cerr << "EventQueue: 批量处理异常: " << e.what() << std::endl;
            }
        }
    }

private:
    event_base* base_;         // 外部传入的 event_base（不拥有）
    
    mutable std::mutex mutex_;
    std::queue<T> queue_;
    size_t max_size_;
    std::atomic<bool> stopped_;
    
    int notify_fd_;            // 通知文件描述符
#if !USE_EVENTFD
    int pipe_fds_[2];          // macOS 使用 pipe
#endif
    struct event* event_;      // libevent 事件对象
    
    // 回调函数（两种模式二选一）
    EventCallback event_callback_;       // 单项处理回调
    BatchCallback batch_callback_;       // 批量处理回调
    size_t batch_size_;                  // 批量处理大小
};

}  // namespace EventBase

#endif // EVENT_QUEUE_H
