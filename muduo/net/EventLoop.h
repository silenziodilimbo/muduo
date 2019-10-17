// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_EVENTLOOP_H
#define MUDUO_NET_EVENTLOOP_H

#include <atomic>
#include <functional>
#include <vector>

#include <boost/any.hpp>

#include <muduo/base/Mutex.h>
#include <muduo/base/CurrentThread.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Callbacks.h>
#include <muduo/net/TimerId.h>

namespace muduo
{
namespace net
{

class Channel;
class Poller;
class TimerQueue;

///
/// Reactor, at most one per thread.
///
/// This is an interface class, so don't expose too much details.
class EventLoop : noncopyable
{
 public:
  typedef std::function<void()> Functor;

  EventLoop();
  ~EventLoop();  // force out-line dtor, for std::unique_ptr members.

  ///
  /// Loops forever.
  ///
  /// Must be called in the same thread as creation of the object.
  ///
  // 开始loop
  void loop();

  /// Quits loop.
  ///
  /// This is not 100% thread safe, if you call through a raw pointer,
  /// better to call through shared_ptr<EventLoop> for 100% safety.
  // 退出loop
  void quit();

  ///
  /// Time when poll returns, usually means data arrival.
  ///
  Timestamp pollReturnTime() const { return pollReturnTime_; }

  int64_t iteration() const { return iteration_; }

  /// Runs callback immediately in the loop thread.
  /// It wakes up the loop, and run the cb.
  /// If in the same loop thread, cb is run within the function.
  /// Safe to call from other threads.\
  // 可以被本IO线程或其他线程调用
  // 让cb在IO线程的EventLoop中执行回调函数
  void runInLoop(Functor cb);
  /// Queues callback in the loop thread.
  /// Runs after finish pooling.
  /// Safe to call from other threads.
  void queueInLoop(Functor cb);

  size_t queueSize() const;

  // timers

  ///
  /// Runs callback at 'time'.
  /// Safe to call from other threads.
  ///
  TimerId runAt(Timestamp time, TimerCallback cb);
  ///
  /// Runs callback after @c delay seconds.
  /// Safe to call from other threads.
  ///
  TimerId runAfter(double delay, TimerCallback cb);
  ///
  /// Runs callback every @c interval seconds.
  /// Safe to call from other threads.
  ///
  TimerId runEvery(double interval, TimerCallback cb);
  ///
  /// Cancels the timer.
  /// Safe to call from other threads.
  ///
  void cancel(TimerId timerId);

  // internal usage
  void wakeup();
  void updateChannel(Channel* channel); // 由channel调用, 用于向本loop添加/删除自己(事件), 实际上调用了poller的updateChannel
  void removeChannel(Channel* channel); // 由channel调用, 用于向本loop添加/删除自己(事件), 实际上调用了poller的removeChannel
  bool hasChannel(Channel* channel); // 由channel调用, 用于向本loop添加/删除自己(事件)

  // pid_t threadId() const { return threadId_; }
  void assertInLoopThread()
  {
    if (!isInLoopThread())
    {
      abortNotInLoopThread();
    }
  }
  // 检测当前线程是否已经创建了其他EventLoop对象
  // 在构造函数中, 记住了本对象所在的线程threadId_, 也是IO线程
  bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }
  // bool callingPendingFunctors() const { return callingPendingFunctors_; }
  bool eventHandling() const { return eventHandling_; }

  void setContext(const boost::any& context)
  { context_ = context; }

  const boost::any& getContext() const
  { return context_; }

  boost::any* getMutableContext()
  { return &context_; }

  // 如果当前线程不是IO线程的话, 就会返回NULL
  static EventLoop* getEventLoopOfCurrentThread();

 private:
  void abortNotInLoopThread();
  // 读下8字节数据
  void handleRead();  // waked up
  // 处理队列中的回调函数
  void doPendingFunctors();

  void printActiveChannels() const; // DEBUG

  typedef std::vector<Channel*> ChannelList;

  bool looping_; /* atomic */ // 用于标识是否正在looping中
  std::atomic<bool> quit_; // 是否退出
  bool eventHandling_;  // 是否正在处理事件, 防止执行remove /* atomic */
  bool callingPendingFunctors_; /* atomic */
  int64_t iteration_;
  const pid_t threadId_;
  Timestamp pollReturnTime_;
  std::unique_ptr<Poller> poller_;
  std::unique_ptr<TimerQueue> timerQueue_;
  // 创建一个唤醒事件fd
  int wakeupFd_;
  // unlike in TimerQueue, which is an internal class,
  // we don't expose Channel to client.
  // 用于处理wakeupFd_上的readable时间, 将时间分发至handleRead()函数
  std::unique_ptr<Channel> wakeupChannel_;
  boost::any context_;

  // scratch variables
  ChannelList activeChannels_;
  Channel* currentActiveChannel_;

  mutable MutexLock mutex_;
  std::vector<Functor> pendingFunctors_; // 暴露给了其他线程 GUARDED_BY(mutex_);
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_EVENTLOOP_H
