// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_TIMERQUEUE_H
#define MUDUO_NET_TIMERQUEUE_H

#include <set>
#include <vector>

#include <muduo/base/Mutex.h>
#include <muduo/base/Timestamp.h>
#include <muduo/net/Callbacks.h>
#include <muduo/net/Channel.h>

namespace muduo
{
namespace net
{

class EventLoop;
class Timer;
class TimerId;

///
/// A best efforts timer queue.
/// No guarantee that the callback will be on time.
///
class TimerQueue : noncopyable
{
 public:
  explicit TimerQueue(EventLoop* loop);
  ~TimerQueue();

  ///
  /// Schedules the callback to be run at given time,
  /// repeats if @c interval > 0.0.
  ///
  /// Must be thread safe. Usually be called from other threads.
  TimerId addTimer(TimerCallback cb,
                   Timestamp when,
                   double interval);

  void cancel(TimerId timerId);

 private:

  // FIXME: use unique_ptr<Timer> instead of raw pointers.
  // This requires heterogeneous comparison lookup (N3465) from C++14
  // so that we can find an T* in a set<unique_ptr<T>>.
  // 为了解决无法处理两个Timer到期时间相同的情况。使用了pair将时间戳和Timer的地址组成了一对.然后使用Set存储.
  typedef std::pair<Timestamp, Timer*> Entry;
  typedef std::set<Entry> TimerList;
  // ActiveTimer 将Timer和sequence组成一对主要作用来索引迭代的.
  typedef std::pair<Timer*, int64_t> ActiveTimer;
  typedef std::set<ActiveTimer> ActiveTimerSet;

  // 由EventLoop调用, 被封装为更好用的runAt(), runAfter(), runEvery()等函数
  void addTimerInLoop(Timer* timer);
  void cancelInLoop(TimerId timerId);
  // called when timerfd alarms
  void handleRead();
  // move out all expired timers
  std::vector<Entry> getExpired(Timestamp now);
  void reset(const std::vector<Entry>& expired, Timestamp now);

  bool insert(Timer* timer);

  EventLoop* loop_;
  // 这是linux上的定时器fd, 系统定时器
  // 将超时任务转换成文件描述符进行监听
  // 统一到io复用函数中，达到统一的效果
  const int timerfd_;
  // 用于观察timerfd_上的readable事件
  Channel timerfdChannel_;
  // Timer list sorted by expiration
  // 为了解决无法处理两个Timer到期时间相同的情况。使用了pair将时间戳和Timer的地址组成了一对.然后使用Set存储.
  // std::set<Entry>, 只有key没有value
  TimerList timers_;

  // for cancel()
 // ActiveTimer 将Timer和sequence组成一对主要作用来索引迭代的.
  ActiveTimerSet activeTimers_;
  bool callingExpiredTimers_; /* atomic */
  // 等待取消队列, 因为当前正在处理这个定时器, 所以没法再cancel->cancelInLoop()中取消
  ActiveTimerSet cancelingTimers_;
};

}  // namespace net
}  // namespace muduo
#endif  // MUDUO_NET_TIMERQUEUE_H
