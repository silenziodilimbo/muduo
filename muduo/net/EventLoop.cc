// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/EventLoop.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
#include <muduo/net/Channel.h>
#include <muduo/net/Poller.h>
#include <muduo/net/SocketsOps.h>
#include <muduo/net/TimerQueue.h>

#include <algorithm>

#include <signal.h>
#include <sys/eventfd.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

// ���������ռ�
// internal��������, �Զ�using, ���������������ڵ�ǰ�ļ���
// һ���̶������static����
namespace
{
__thread EventLoop* t_loopInThisThread = 0;

// pollĬ��ʱ��
const int kPollTimeMs = 10000;

// ����һ�������¼�fd
int createEventfd()
{
  int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evtfd < 0)
  {
    LOG_SYSERR << "Failed in eventfd";
    abort();
  }
  return evtfd;
}

#pragma GCC diagnostic ignored "-Wold-style-cast"
// ����SigPipe�źţ���ֹ�ж��˳�
class IgnoreSigPipe
{
 public:
  IgnoreSigPipe()
  {
    ::signal(SIGPIPE, SIG_IGN);
    // LOG_TRACE << "Ignore SIGPIPE";
  }
};
#pragma GCC diagnostic error "-Wold-style-cast"

IgnoreSigPipe initObj;
}  // namespace

// �����ǰ�̲߳���IO�̵߳Ļ�, �ͻ᷵��NULL
EventLoop* EventLoop::getEventLoopOfCurrentThread()
{
  return t_loopInThisThread;
}

EventLoop::EventLoop()
  : looping_(false),
    quit_(false),
    eventHandling_(false),
    callingPendingFunctors_(false),
    iteration_(0),
    threadId_(CurrentThread::tid()), // ���洴��������߳�, ����ȷ��one loop per thread
    poller_(Poller::newDefaultPoller(this)), // ����һ��polloer
    timerQueue_(new TimerQueue(this)), // ����һ����ʱ������
    wakeupFd_(createEventfd()), // ����һ�������¼�fd
    wakeupChannel_(new Channel(this, wakeupFd_)), // ����һ�������¼�ͨ��
    currentActiveChannel_(NULL)
{
  LOG_DEBUG << "EventLoop created " << this << " in thread " << threadId_;
  // ���õ�ǰloop�ĵ�ַ��
  // �����оͲ����á�
  if (t_loopInThisThread) 
  {
    // ����������EventLoop
    LOG_FATAL << "Another EventLoop " << t_loopInThisThread
              << " exists in this thread " << threadId_;
  }
  else
  {
    // Ĭ��Ϊ0, ����������namespace
    // ��һ�λ������Ϊ��0, ��ֹ�ظ�����
    t_loopInThisThread = this;
  }
  // ���û���ͨ���Ļص�����FIXME_huqibing �������Ӧ����������;
  wakeupChannel_->setReadCallback(
      std::bind(&EventLoop::handleRead, this));
  // we are always reading the wakeupfd
  wakeupChannel_->enableRe..ading();
}

// ���� 
// �رվ��
// ��t_loopInThisThread��λ�յ�ַ
EventLoop::~EventLoop()
{
  LOG_DEBUG << "EventLoop " << this << " of thread " << threadId_
            << " destructs in thread " << CurrentThread::tid();
  wakeupChannel_->disableAll();
  wakeupChannel_->remove();
  ::close(wakeupFd_);
  t_loopInThisThread = NULL;  // һ����̬���߳�id
}

// һ��whileѭ��
// ����������
// 1��ִ��ִ��activeChannel������¼��ص�
// ��Channel::handleEvent
// 2��ִ�������߳�runInLoop�Ž����ĺ���
// ��doPendingFunctors
void EventLoop::loop()
{
  assert(!looping_);
  assertInLoopThread();

  // ����״̬λ
  looping_ = true;
  quit_ = false;  // FIXME: what if someone calls quit() before loop() ?
  LOG_TRACE << "EventLoop " << this << " start looping";

  // ��ʼѭ��
  while (!quit_)
  {
    // һ��ѭ����ʼ

    // ��֮ǰ��List���
    activeChannels_.clear(); 
    // ��ȡ�µ�List
    pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
    ++iteration_; // todo���Ǹ����
    if (Logger::logLevel() <= Logger::TRACE)
    {
      printActiveChannels();
    }
    // TODO sort channel by priority
    eventHandling_ = true; // ��, ��ִֹ��remove /* atomic */
    // ��ѯ���List, ����Handler
    for (Channel* channel : activeChannels_)
    {
      currentActiveChannel_ = channel;
      currentActiveChannel_->handleEvent(pollReturnTime_); // ���뵱ǰʱ��
    }
    currentActiveChannel_ = NULL;
    eventHandling_ = false;
    // �����¶������Ƿ�����Ҫִ�еĻص�����
    doPendingFunctors();
  }

  //�����������, ����ѭ��������
  LOG_TRACE << "EventLoop " << this << " stop looping";
  looping_ = false;
}

void EventLoop::quit()
{
  // ״̬λ
  quit_ = true;
  // There is a chance that loop() just executes while(!quit_) and exits,
  // then EventLoop destructs, then we are accessing an invalid object.
  // Can be fixed using mutex_ in both places.
  // ����ڵ�ǰ�߳�, �ͻ�����fd?
  if (!isInLoopThread())
  {
    wakeup();
  }
}

// ���Ա���IO�̻߳������̵߳���
// ��cb��IO�̵߳�EventLoop��ִ�лص�����
// ����ڵ�ǰIO�̵߳����������, �ص���ͬ������
// ����������̵߳���runInLoop(), cb���¼������, IO�̻߳ᱻ�������������cb  
void EventLoop::runInLoop(Functor cb)
{
  if (isInLoopThread())
  {
    cb();
  }
  else
  {
    queueInLoop(std::move(cb));
  }
}

void EventLoop::queueInLoop(Functor cb)
{
  // ���ص������ŵ���ִ�ж���
  {
  // pendingFunctors_��¶���������߳�
  MutexLockGuard lock(mutex_);
  pendingFunctors_.push_back(std::move(cb)); // �������������
  }
  
  // ��Ҫʱ�����߳�, �������
  // �����ǰ����IO�߳���, �����߳�
  // ��ʱ���ڵ���pending function, Ҳ����
  // ֻ����IO�̵߳��¼��ص��е���queueInLoop(), ������wakeup()
  if (!isInLoopThread() || callingPendingFunctors_)
  {
    wakeup();
  }
}

size_t EventLoop::queueSize() const
{
  MutexLockGuard lock(mutex_);
  return pendingFunctors_.size();
}

// ��ִ�лص���ӵ���ʱ������
// ������߳�ʹ��
// muduoû�м���, ���ǰ�TimerQueue�Ĳ���ת�Ƶ���IO�߳�������
TimerId EventLoop::runAt(Timestamp time, TimerCallback cb)
{
  return timerQueue_->addTimer(std::move(cb), time, 0.0);
}

TimerId EventLoop::runAfter(double delay, TimerCallback cb)
{
  Timestamp time(addTime(Timestamp::now(), delay));
  return runAt(time, std::move(cb));
}

TimerId EventLoop::runEvery(double interval, TimerCallback cb)
{
  Timestamp time(addTime(Timestamp::now(), interval));
  return timerQueue_->addTimer(std::move(cb), time, interval);
}

// ȡ��ĳ����ʱ������, ���timerId��֮ǰrunAt���ص�
void EventLoop::cancel(TimerId timerId)
{
  return timerQueue_->cancel(timerId);
}

// ����ͨ��
// ��channel::update����
// ��������һ�仰
// poller_->updateChannel(channel);
void EventLoop::updateChannel(Channel* channel)
{
  // Channel���ڵ�LoopҪ�͵�ǰLoop����һ��
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  poller_->updateChannel(channel);
}

// �Ƴ�ͨ��
// ��channel::remove����
// ��������һ�仰
// poller_->removeChannel(channel);
void EventLoop::removeChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  if (eventHandling_)
  {
    // �����ǰ�ڴ���ͨ���¼�ʱ��Ҫɾ��Channel
    // �����ɾ���¼�һ��ʱ��ǰ�����HandleEvent�����
    // Ҳ����˵currentActiveChannel_ �͵�ǰͨ����ͬһ����
    // ����ͨ�����ڻ�Ծ��ͨ���б���
    assert(currentActiveChannel_ == channel ||
        std::find(activeChannels_.begin(), activeChannels_.end(), channel) == activeChannels_.end());
  }
  poller_->removeChannel(channel);
}

bool EventLoop::hasChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  return poller_->hasChannel(channel);
}

void EventLoop::abortNotInLoopThread()
{
  LOG_FATAL << "EventLoop::abortNotInLoopThread - EventLoop " << this
            << " was created in threadId_ = " << threadId_
            << ", current thread id = " <<  CurrentThread::tid();
}

// �����õ���һ����ƾ��������
// �ص����������У�ͨ����fdд��
// ��־��֪ͨ����������Poll����
// ����ȥִ�лص�������

// �����£�дһ��8�ֽ�����
void EventLoop::wakeup()
{
  uint64_t one = 1;
  size_t n = sockets::write(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
  {
    LOG_ERROR << "EventLoop::wakeup() writes " << n << " bytes instead of 8";
  }
}

// ����8�ֽ�����
// ��û�����������doPendingFunctors
// EventLoop::handleRead()ֻ���ڵ�����EventLoop::wakeup()���ܱ�ִ�С�
// ���doPendingFunctors()����EventLoop::handleRead()�ڱ����ã�
// ��ô��IO�߳���ע���˻ص���������û�е���EventLoop::wakeup()��
// ��ô�ص��������ᱻ�����õ�ִ�У�������ȴ�EventLoop::wakeup()�����ú���ܱ�ִ�С�
void EventLoop::handleRead()
{
  uint64_t one = 1;
  size_t n = sockets::read(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
  {
    LOG_ERROR << "EventLoop::handleRead() reads " << n << " bytes instead of 8";
  }
}

// EventLoop::loop()����
// ��������еĻص�����
// ��������ִ��һ��
// һ����������swap����ʱ������, ��ִ��, ���ⳤʱ���������
void EventLoop::doPendingFunctors()
{
  // ����swap�ص��б�
  std::vector<Functor> functors;
  // FIXME_hqb ״̬��־��������ʲô
  callingPendingFunctors_ = true;

  // �����������������������̵߳���queueInLoop()
  // ����������, Functor���ܻ��ٵ���queueInLoop(), �ͻ�����������
  {
  MutexLockGuard lock(mutex_);
  functors.swap(pendingFunctors_);
  }

  for (const Functor& functor : functors)
  {
    functor();
  }
  callingPendingFunctors_ = false;
}

// ����»�Ծ��ͨ����Ӧ���¼������ڵ���
void EventLoop::printActiveChannels() const
{
  for (const Channel* channel : activeChannels_)
  {
    LOG_TRACE << "{" << channel->reventsToString() << "} ";
  }
}

