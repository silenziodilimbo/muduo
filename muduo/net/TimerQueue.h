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
  // Ϊ�˽���޷���������Timer����ʱ����ͬ�������ʹ����pair��ʱ�����Timer�ĵ�ַ�����һ��.Ȼ��ʹ��Set�洢.
  typedef std::pair<Timestamp, Timer*> Entry;
  typedef std::set<Entry> TimerList;
  // ActiveTimer ��Timer��sequence���һ����Ҫ����������������.
  typedef std::pair<Timer*, int64_t> ActiveTimer;
  typedef std::set<ActiveTimer> ActiveTimerSet;

  // ��EventLoop����, ����װΪ�����õ�runAt(), runAfter(), runEvery()�Ⱥ���
  void addTimerInLoop(Timer* timer);
  void cancelInLoop(TimerId timerId);
  // called when timerfd alarms
  void handleRead();
  // move out all expired timers
  std::vector<Entry> getExpired(Timestamp now);
  void reset(const std::vector<Entry>& expired, Timestamp now);

  bool insert(Timer* timer);

  EventLoop* loop_;
  // ����linux�ϵĶ�ʱ��fd, ϵͳ��ʱ��
  // ����ʱ����ת�����ļ����������м���
  // ͳһ��io���ú����У��ﵽͳһ��Ч��
  const int timerfd_;
  // ���ڹ۲�timerfd_�ϵ�readable�¼�
  Channel timerfdChannel_;
  // Timer list sorted by expiration
  // Ϊ�˽���޷���������Timer����ʱ����ͬ�������ʹ����pair��ʱ�����Timer�ĵ�ַ�����һ��.Ȼ��ʹ��Set�洢.
  // std::set<Entry>, ֻ��keyû��value
  TimerList timers_;

  // for cancel()
 // ActiveTimer ��Timer��sequence���һ����Ҫ����������������.
  ActiveTimerSet activeTimers_;
  bool callingExpiredTimers_; /* atomic */
  // �ȴ�ȡ������, ��Ϊ��ǰ���ڴ��������ʱ��, ����û����cancel->cancelInLoop()��ȡ��
  ActiveTimerSet cancelingTimers_;
};

}  // namespace net
}  // namespace muduo
#endif  // MUDUO_NET_TIMERQUEUE_H
