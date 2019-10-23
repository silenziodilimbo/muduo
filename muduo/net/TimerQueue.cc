// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif

#include <muduo/net/TimerQueue.h>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Timer.h>
#include <muduo/net/TimerId.h>

#include <sys/timerfd.h>
#include <unistd.h>

namespace muduo
{
namespace net
{
namespace detail
{

int createTimerfd()
{
  int timerfd = ::timerfd_create(CLOCK_MONOTONIC,
                                 TFD_NONBLOCK | TFD_CLOEXEC);
  if (timerfd < 0)
  {
    LOG_SYSFATAL << "Failed in timerfd_create";
  }
  return timerfd;
}

struct timespec howMuchTimeFromNow(Timestamp when)
{
  int64_t microseconds = when.microSecondsSinceEpoch()
                         - Timestamp::now().microSecondsSinceEpoch();
  if (microseconds < 100)
  {
    microseconds = 100;
  }
  struct timespec ts;
  ts.tv_sec = static_cast<time_t>(
      microseconds / Timestamp::kMicroSecondsPerSecond);
  ts.tv_nsec = static_cast<long>(
      (microseconds % Timestamp::kMicroSecondsPerSecond) * 1000);
  return ts;
}

// 定时器到期，读取一次句柄
void readTimerfd(int timerfd, Timestamp now)
{
  uint64_t howmany;
  size_t n = ::read(timerfd, &howmany, sizeof howmany);
  LOG_TRACE << "TimerQueue::handleRead() " << howmany << " at " << now.toString();
  if (n != sizeof howmany)
  {
    LOG_ERROR << "TimerQueue::handleRead() reads " << n << " bytes instead of 8";
  }
}

// 此方法很简单，就是调用linux系统函数，重新设置下到期时间
void resetTimerfd(int timerfd, Timestamp expiration)
{
  // wake up loop by timerfd_settime()
  struct itimerspec newValue;
  struct itimerspec oldValue;
  memZero(&newValue, sizeof newValue);
  memZero(&oldValue, sizeof oldValue);
  newValue.it_value = howMuchTimeFromNow(expiration);
  int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);
  if (ret)
  {
    LOG_SYSERR << "timerfd_settime()";
  }
}

}  // namespace detail
}  // namespace net
}  // namespace muduo

using namespace muduo;
using namespace muduo::net;
using namespace muduo::net::detail;

// TimerQueue实例化时设置好定时器回调。这里用到了Muduo通道Channel方法。
TimerQueue::TimerQueue(EventLoop* loop)
  : loop_(loop),
    timerfd_(createTimerfd()),
    timerfdChannel_(loop, timerfd_),
    timers_(),
    callingExpiredTimers_(false)
{
  timerfdChannel_.setReadCallback(
      std::bind(&TimerQueue::handleRead, this));
  // we are always reading the timerfd, we disarm it with timerfd_settime.
  timerfdChannel_.enableReading();
}

TimerQueue::~TimerQueue()
{
  timerfdChannel_.disableAll();
  timerfdChannel_.remove();
  ::close(timerfd_);
  // do not remove channel, since we're in EventLoop::dtor();
  for (const Entry& timer : timers_)
  {
    delete timer.second;
  }
}

// cb 定时器到期执行函数，when 定时器到期时间，interval非零表示重复定时器
TimerId TimerQueue::addTimer(TimerCallback cb,
                             Timestamp when,
                             double interval)
{
  // 实例化一个定期器类
  Timer* timer = new Timer(std::move(cb), when, interval);
  // addTimer()只负责转发
  // addTimerInLoop()完成修改定时器列表的工作
  // 将addTimerInLoop方法放到EventLoop中执行
  // 若用户在当前IO线程，回调则同步执行，否则将方法加入到队列，
  loop_->runInLoop(
      std::bind(&TimerQueue::addTimerInLoop, this, timer));
  // 实例化一个定时器和序列号封装的TimerId进行返回，用于用户取消定时器
  return TimerId(timer, timer->sequence());
}

void TimerQueue::cancel(TimerId timerId)
{
  loop_->runInLoop(
      std::bind(&TimerQueue::cancelInLoop, this, timerId));
}

void TimerQueue::addTimerInLoop(Timer* timer)
{
  loop_->assertInLoopThread();
  // 插入一个定时器，并返回新添加的定时器是不是比队列里已存在的所有定时器过期时间还早
  bool earliestChanged = insert(timer);

  if (earliestChanged)
  {
    // 如果新入队的定时器是队列里最早的，从新设置下系统定时器到期触发时间
    resetTimerfd(timerfd_, timer->expiration());
  }
}

void TimerQueue::cancelInLoop(TimerId timerId)
{
  loop_->assertInLoopThread();
  assert(timers_.size() == activeTimers_.size());
  // 用ActiveTimer来索引
  ActiveTimer timer(timerId.timer_, timerId.sequence_);
  ActiveTimerSet::iterator it = activeTimers_.find(timer);
  // 从非过期的队列里找
  if (it != activeTimers_.end())
  {
    // 从队列中找到，然后删除
    // 从timers_里删除
    size_t n = timers_.erase(Entry(it->first->expiration(), it->first));
    assert(n == 1); (void)n;
    delete it->first; // FIXME: no delete please
    // 从activeTimers_里删除
    activeTimers_.erase(it);
  }
  else if (callingExpiredTimers_)
  {
    // 如果没找到, 进入了else, 就是已经过期了
    // 检测是否正在执行, 正在handleRead()中
    // 将要取消的定时器放入取消定时器队列，TimerQueue->reset会用到。

    // cancelingTimers_和callingExpiredTimers_是为了应对“自注销”这种情况。
    // 即, 一个定时器, 的回调函数, 是注销自己, 
    // 但是因为这个定时器在执行回调的时候, 已经移动到了getExpired方法的expired中
    // loop_->cancel无法注销
    // https://blog.csdn.net/H514434485/article/details/90147515
    cancelingTimers_.insert(timer);
  }
  assert(timers_.size() == activeTimers_.size());
}

void TimerQueue::handleRead()
{
  loop_->assertInLoopThread();
  Timestamp now(Timestamp::now());
  // 定时器到期，读取一次句柄
  readTimerfd(timerfd_, now);
  // 从timers_中移除已到期的Timer, 并通过vector返回它们
  std::vector<Entry> expired = getExpired(now);

  // 处于定时器处理状态中
  callingExpiredTimers_ = true;
  // 在cancelInLoop中, 用户主动取消了一个定时器
  // 先从timers_中寻找, 如果找到了, 直接删除
  // 如果没找到, 那么可能该定时器已经过期, 已经在getExpired()中被转移到expired中, 等待处理
  // cancelInLoop检测如果当前正在执行handler
  // 就暂时存放在cancelingTimers_里, 在这里直接clear()析构掉
  cancelingTimers_.clear();
  // safe to callback outside critical section
  // 执行每个到期的定时器方法
  for (const Entry& it : expired)
  {
    it.second->run();
  }
  callingExpiredTimers_ = false;

  // 检查expired
  // 重置过期定时器状态，如果是重复执行定时器就再入队，否则删除
  // expired是所有到期的时间的vector
  reset(expired, now);
}

// 关键函数
// 从timers_中移除已到期的Timer, 并通过vector返回它们
std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
{
  assert(timers_.size() == activeTimers_.size());
  // 暂时存放已过期
  std::vector<Entry> expired;
  // sentry哨兵值
  Entry sentry(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
  // 这里用到了std::set::lower_bound（寻找第一个大于等于Value的值），
  // sentry让set::lower_bound返回的是寻找第一个大于等于Value的值, 即第一个未到期的Timer的迭代器
  // end是一个大于sentry的值，所以是<判断。
  // 所以assert是<而非=<
  TimerList::iterator end = timers_.lower_bound(sentry);
  // end是最后一个, 没过期?
  assert(end == timers_.end() || now < end->first);
  // 拷贝已经到期的到expired
  std::copy(timers_.begin(), end, back_inserter(expired));
  // 删除这些已过期的
  timers_.erase(timers_.begin(), end);


  for (const Entry& it : expired)
  {
    ActiveTimer timer(it.second, it.second->sequence());
    size_t n = activeTimers_.erase(timer);
    assert(n == 1); (void)n;
  }

  assert(timers_.size() == activeTimers_.size());
  return expired;
}

// 检查expired
// 重置过期定时器状态，如果是重复执行定时器就再入队，否则删除
// expired是所有到期的时间的vector
void TimerQueue::reset(const std::vector<Entry>& expired, Timestamp now)
{
  Timestamp nextExpire;

  for (const Entry& it : expired)
  {
    ActiveTimer timer(it.second, it.second->sequence());
    if (it.second->repeat()
        && cancelingTimers_.find(timer) == cancelingTimers_.end())
    {
      // 如果是重复执行定时器就再入队
      // 1、定时器是重复定时器
      // 2、取消定时器队列中无此定时器。如果用户手动删除了这个定时任务，就不添加了
      // 则此定时器再入队

      // 重新计算超时时间 
      it.second->restart(now);
      // 插入定时器
      insert(it.second);
    }
    else
    {
      // 否则删除
      // FIXME move to a free list
      delete it.second; // FIXME: no delete please
    }
  }

  if (!timers_.empty())
  {
    // 获取当前定时器队列中第一个（即最早过期时间）定时器
    nextExpire = timers_.begin()->second->expiration();
  }

  if (nextExpire.valid())
  {
    // 重新设置下系统定时器时间
    resetTimerfd(timerfd_, nextExpire);
  }
}

bool TimerQueue::insert(Timer* timer)
{
  loop_->assertInLoopThread();
  // timers_和activeTimers_存着同样的定时器列表，个数是一样的
  assert(timers_.size() == activeTimers_.size()); 
  // timers_是按过期时间有序排列的，最早到期的在前面
  // 新插入的时间和队列中最早到期时间比，判断新插入时间是否更早
  bool earliestChanged = false;
  // 定时器过期时间
  Timestamp when = timer->expiration();
  TimerList::iterator it = timers_.begin();
  // timers只有一个定时器, 且当前定时器确实更早
  if (it == timers_.end() || when < it->first)
  {
    earliestChanged = true;
  }
  // 将时间保存入队，std::set自动保存有序
  {
    std::pair<TimerList::iterator, bool> result
      = timers_.insert(Entry(when, timer));
    assert(result.second); (void)result;
  }
  // 同步放入activeTimers_
  {
    std::pair<ActiveTimerSet::iterator, bool> result
      = activeTimers_.insert(ActiveTimer(timer, timer->sequence()));
    assert(result.second); (void)result;
  }

  assert(timers_.size() == activeTimers_.size());
  return earliestChanged;
}

