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

// ��ʱ�����ڣ���ȡһ�ξ��
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

// �˷����ܼ򵥣����ǵ���linuxϵͳ���������������µ���ʱ��
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

// TimerQueueʵ����ʱ���úö�ʱ���ص��������õ���Muduoͨ��Channel������
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

// cb ��ʱ������ִ�к�����when ��ʱ������ʱ�䣬interval�����ʾ�ظ���ʱ��
TimerId TimerQueue::addTimer(TimerCallback cb,
                             Timestamp when,
                             double interval)
{
  // ʵ����һ����������
  Timer* timer = new Timer(std::move(cb), when, interval);
  // addTimer()ֻ����ת��
  // addTimerInLoop()����޸Ķ�ʱ���б�Ĺ���
  // ��addTimerInLoop�����ŵ�EventLoop��ִ��
  // ���û��ڵ�ǰIO�̣߳��ص���ͬ��ִ�У����򽫷������뵽���У�
  loop_->runInLoop(
      std::bind(&TimerQueue::addTimerInLoop, this, timer));
  // ʵ����һ����ʱ�������кŷ�װ��TimerId���з��أ������û�ȡ����ʱ��
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
  // ����һ����ʱ��������������ӵĶ�ʱ���ǲ��Ǳȶ������Ѵ��ڵ����ж�ʱ������ʱ�仹��
  bool earliestChanged = insert(timer);

  if (earliestChanged)
  {
    // �������ӵĶ�ʱ���Ƕ���������ģ�����������ϵͳ��ʱ�����ڴ���ʱ��
    resetTimerfd(timerfd_, timer->expiration());
  }
}

void TimerQueue::cancelInLoop(TimerId timerId)
{
  loop_->assertInLoopThread();
  assert(timers_.size() == activeTimers_.size());
  // ��ActiveTimer������
  ActiveTimer timer(timerId.timer_, timerId.sequence_);
  ActiveTimerSet::iterator it = activeTimers_.find(timer);
  // �ӷǹ��ڵĶ�������
  if (it != activeTimers_.end())
  {
    // �Ӷ������ҵ���Ȼ��ɾ��
    // ��timers_��ɾ��
    size_t n = timers_.erase(Entry(it->first->expiration(), it->first));
    assert(n == 1); (void)n;
    delete it->first; // FIXME: no delete please
    // ��activeTimers_��ɾ��
    activeTimers_.erase(it);
  }
  else if (callingExpiredTimers_)
  {
    // ���û�ҵ�, ������else, �����Ѿ�������
    // ����Ƿ�����ִ��, ����handleRead()��
    // ��Ҫȡ���Ķ�ʱ������ȡ����ʱ�����У�TimerQueue->reset���õ���

    // cancelingTimers_��callingExpiredTimers_��Ϊ��Ӧ�ԡ���ע�������������
    // ��, һ����ʱ��, �Ļص�����, ��ע���Լ�, 
    // ������Ϊ�����ʱ����ִ�лص���ʱ��, �Ѿ��ƶ�����getExpired������expired��
    // loop_->cancel�޷�ע��
    // https://blog.csdn.net/H514434485/article/details/90147515
    cancelingTimers_.insert(timer);
  }
  assert(timers_.size() == activeTimers_.size());
}

void TimerQueue::handleRead()
{
  loop_->assertInLoopThread();
  Timestamp now(Timestamp::now());
  // ��ʱ�����ڣ���ȡһ�ξ��
  readTimerfd(timerfd_, now);
  // ��timers_���Ƴ��ѵ��ڵ�Timer, ��ͨ��vector��������
  std::vector<Entry> expired = getExpired(now);

  // ���ڶ�ʱ������״̬��
  callingExpiredTimers_ = true;
  // ��cancelInLoop��, �û�����ȡ����һ����ʱ��
  // �ȴ�timers_��Ѱ��, ����ҵ���, ֱ��ɾ��
  // ���û�ҵ�, ��ô���ܸö�ʱ���Ѿ�����, �Ѿ���getExpired()�б�ת�Ƶ�expired��, �ȴ�����
  // cancelInLoop��������ǰ����ִ��handler
  // ����ʱ�����cancelingTimers_��, ������ֱ��clear()������
  cancelingTimers_.clear();
  // safe to callback outside critical section
  // ִ��ÿ�����ڵĶ�ʱ������
  for (const Entry& it : expired)
  {
    it.second->run();
  }
  callingExpiredTimers_ = false;

  // ���expired
  // ���ù��ڶ�ʱ��״̬��������ظ�ִ�ж�ʱ��������ӣ�����ɾ��
  // expired�����е��ڵ�ʱ���vector
  reset(expired, now);
}

// �ؼ�����
// ��timers_���Ƴ��ѵ��ڵ�Timer, ��ͨ��vector��������
std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now)
{
  assert(timers_.size() == activeTimers_.size());
  // ��ʱ����ѹ���
  std::vector<Entry> expired;
  // sentry�ڱ�ֵ
  Entry sentry(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
  // �����õ���std::set::lower_bound��Ѱ�ҵ�һ�����ڵ���Value��ֵ����
  // sentry��set::lower_bound���ص���Ѱ�ҵ�һ�����ڵ���Value��ֵ, ����һ��δ���ڵ�Timer�ĵ�����
  // end��һ������sentry��ֵ��������<�жϡ�
  // ����assert��<����=<
  TimerList::iterator end = timers_.lower_bound(sentry);
  // end�����һ��, û����?
  assert(end == timers_.end() || now < end->first);
  // �����Ѿ����ڵĵ�expired
  std::copy(timers_.begin(), end, back_inserter(expired));
  // ɾ����Щ�ѹ��ڵ�
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

// ���expired
// ���ù��ڶ�ʱ��״̬��������ظ�ִ�ж�ʱ��������ӣ�����ɾ��
// expired�����е��ڵ�ʱ���vector
void TimerQueue::reset(const std::vector<Entry>& expired, Timestamp now)
{
  Timestamp nextExpire;

  for (const Entry& it : expired)
  {
    ActiveTimer timer(it.second, it.second->sequence());
    if (it.second->repeat()
        && cancelingTimers_.find(timer) == cancelingTimers_.end())
    {
      // ������ظ�ִ�ж�ʱ���������
      // 1����ʱ�����ظ���ʱ��
      // 2��ȡ����ʱ���������޴˶�ʱ��������û��ֶ�ɾ���������ʱ���񣬾Ͳ������
      // ��˶�ʱ�������

      // ���¼��㳬ʱʱ�� 
      it.second->restart(now);
      // ���붨ʱ��
      insert(it.second);
    }
    else
    {
      // ����ɾ��
      // FIXME move to a free list
      delete it.second; // FIXME: no delete please
    }
  }

  if (!timers_.empty())
  {
    // ��ȡ��ǰ��ʱ�������е�һ�������������ʱ�䣩��ʱ��
    nextExpire = timers_.begin()->second->expiration();
  }

  if (nextExpire.valid())
  {
    // ����������ϵͳ��ʱ��ʱ��
    resetTimerfd(timerfd_, nextExpire);
  }
}

bool TimerQueue::insert(Timer* timer)
{
  loop_->assertInLoopThread();
  // timers_��activeTimers_����ͬ���Ķ�ʱ���б�������һ����
  assert(timers_.size() == activeTimers_.size()); 
  // timers_�ǰ�����ʱ���������еģ����絽�ڵ���ǰ��
  // �²����ʱ��Ͷ��������絽��ʱ��ȣ��ж��²���ʱ���Ƿ����
  bool earliestChanged = false;
  // ��ʱ������ʱ��
  Timestamp when = timer->expiration();
  TimerList::iterator it = timers_.begin();
  // timersֻ��һ����ʱ��, �ҵ�ǰ��ʱ��ȷʵ����
  if (it == timers_.end() || when < it->first)
  {
    earliestChanged = true;
  }
  // ��ʱ�䱣����ӣ�std::set�Զ���������
  {
    std::pair<TimerList::iterator, bool> result
      = timers_.insert(Entry(when, timer));
    assert(result.second); (void)result;
  }
  // ͬ������activeTimers_
  {
    std::pair<ActiveTimerSet::iterator, bool> result
      = activeTimers_.insert(ActiveTimer(timer, timer->sequence()));
    assert(result.second); (void)result;
  }

  assert(timers_.size() == activeTimers_.size());
  return earliestChanged;
}

