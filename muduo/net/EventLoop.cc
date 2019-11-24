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

// 匿名命名空间
// internal链接属性, 自动using, 而且作用域限制在当前文件内
// 一定程度上替代static变量
namespace
{
__thread EventLoop* t_loopInThisThread = 0;

// poll默认时间
const int kPollTimeMs = 10000;

// 创建一个唤醒事件fd
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
// 屏蔽SigPipe信号，防止中断退出
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

// 如果当前线程不是IO线程的话, 就会返回NULL
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
    threadId_(CurrentThread::tid()), // 保存创建对象的线程, 用于确保one loop per thread
    poller_(Poller::newDefaultPoller(this)), // 创建一个polloer
    timerQueue_(new TimerQueue(this)), // 创建一个定时器队列
    wakeupFd_(createEventfd()), // 创建一个唤醒事件fd
    wakeupChannel_(new Channel(this, wakeupFd_)), // 创建一个唤醒事件通道
    currentActiveChannel_(NULL)
{
  LOG_DEBUG << "EventLoop created " << this << " in thread " << threadId_;
  // 设置当前loop的地址，
  // 若已有就不设置。
  if (t_loopInThisThread) 
  {
    // 出现了两个EventLoop
    LOG_FATAL << "Another EventLoop " << t_loopInThisThread
              << " exists in this thread " << threadId_;
  }
  else
  {
    // 默认为0, 利用了匿名namespace
    // 第一次会把它改为非0, 防止重复创建
    t_loopInThisThread = this;
  }
  // 设置唤醒通道的回调读，FIXME_huqibing 这里设计应该有其它用途
  wakeupChannel_->setReadCallback(
      std::bind(&EventLoop::handleRead, this));
  // we are always reading the wakeupfd
  wakeupChannel_->enableRe..ading();
}

// 析构 
// 关闭句柄
// 将t_loopInThisThread置位空地址
EventLoop::~EventLoop()
{
  LOG_DEBUG << "EventLoop " << this << " of thread " << threadId_
            << " destructs in thread " << CurrentThread::tid();
  wakeupChannel_->disableAll();
  wakeupChannel_->remove();
  ::close(wakeupFd_);
  t_loopInThisThread = NULL;  // 一个静态的线程id
}

// 一个while循环
// 做了两件事
// 1是执行执行activeChannel里面的事件回调
// 即Channel::handleEvent
// 2是执行其他线程runInLoop放进来的函数
// 即doPendingFunctors
void EventLoop::loop()
{
  assert(!looping_);
  assertInLoopThread();

  // 设置状态位
  looping_ = true;
  quit_ = false;  // FIXME: what if someone calls quit() before loop() ?
  LOG_TRACE << "EventLoop " << this << " start looping";

  // 开始循环
  while (!quit_)
  {
    // 一轮循环开始

    // 把之前的List清空
    activeChannels_.clear(); 
    // 获取新的List
    pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
    ++iteration_; // todo这是干嘛的
    if (Logger::logLevel() <= Logger::TRACE)
    {
      printActiveChannels();
    }
    // TODO sort channel by priority
    eventHandling_ = true; // 锁, 防止执行remove /* atomic */
    // 轮询这个List, 交给Handler
    for (Channel* channel : activeChannels_)
    {
      currentActiveChannel_ = channel;
      currentActiveChannel_->handleEvent(pollReturnTime_); // 传入当前时间
    }
    currentActiveChannel_ = NULL;
    eventHandling_ = false;
    // 处理下队列中是否有需要执行的回调方法
    doPendingFunctors();
  }

  //如果到了这里, 就是循环结束了
  LOG_TRACE << "EventLoop " << this << " stop looping";
  looping_ = false;
}

void EventLoop::quit()
{
  // 状态位
  quit_ = true;
  // There is a chance that loop() just executes while(!quit_) and exits,
  // then EventLoop destructs, then we are accessing an invalid object.
  // Can be fixed using mutex_ in both places.
  // 如果在当前线程, 就唤醒下fd?
  if (!isInLoopThread())
  {
    wakeup();
  }
}

// 可以被本IO线程或其他线程调用
// 让cb在IO线程的EventLoop中执行回调函数
// 如果在当前IO线程调用这个函数, 回调会同步进行
// 如果在其他线程调用runInLoop(), cb会呗加入队列, IO线程会被唤醒来调用这个cb  
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
  // 将回调函数放到待执行队列
  {
  // pendingFunctors_暴露给了其他线程
  MutexLockGuard lock(mutex_);
  pendingFunctors_.push_back(std::move(cb)); // 放入这个队列中
  }
  
  // 必要时唤醒线程, 两种情况
  // 如果当前不在IO线程内, 唤醒线程
  // 此时正在调用pending function, 也唤醒
  // 只有再IO线程的事件回调中调用queueInLoop(), 才无须wakeup()
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

// 将执行回调添加到定时器队列
// 允许跨线程使用
// muduo没有加锁, 而是把TimerQueue的操作转移到了IO线程来进行
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

// 取消某个定时器队列, 这个timerId是之前runAt返回的
void EventLoop::cancel(TimerId timerId)
{
  return timerQueue_->cancel(timerId);
}

// 更新通道
// 由channel::update调用
// 基本就是一句话
// poller_->updateChannel(channel);
void EventLoop::updateChannel(Channel* channel)
{
  // Channel所在的Loop要和当前Loop保持一致
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  poller_->updateChannel(channel);
}

// 移除通道
// 由channel::remove调用
// 基本就是一句话
// poller_->removeChannel(channel);
void EventLoop::removeChannel(Channel* channel)
{
  assert(channel->ownerLoop() == this);
  assertInLoopThread();
  if (eventHandling_)
  {
    // 如果当前在处理通道事件时，要删除Channel
    // 则这个删除事件一定时当前处理的HandleEvent发起的
    // 也就是说currentActiveChannel_ 和当前通道是同一个，
    // 而且通道就在活跃的通道列表中
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

// 这里用到的一个设计就是添加了
// 回调函数到队列，通过往fd写个
// 标志来通知，让阻塞的Poll立马
// 返回去执行回调函数。

// 唤醒下，写一个8字节数据
void EventLoop::wakeup()
{
  uint64_t one = 1;
  size_t n = sockets::write(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
  {
    LOG_ERROR << "EventLoop::wakeup() writes " << n << " bytes instead of 8";
  }
}

// 读下8字节数据
// 并没有在这里调用doPendingFunctors
// EventLoop::handleRead()只有在调用了EventLoop::wakeup()才能被执行。
// 如果doPendingFunctors()是在EventLoop::handleRead()内被调用，
// 那么在IO线程内注册了回调函数并且没有调用EventLoop::wakeup()，
// 那么回调函数不会被立即得到执行，而必须等待EventLoop::wakeup()被调用后才能被执行。
void EventLoop::handleRead()
{
  uint64_t one = 1;
  size_t n = sockets::read(wakeupFd_, &one, sizeof one);
  if (n != sizeof one)
  {
    LOG_ERROR << "EventLoop::handleRead() reads " << n << " bytes instead of 8";
  }
}

// EventLoop::loop()调用
// 处理队列中的回调函数
// 基本就是执行一遍
// 一个技巧是先swap到临时对象中, 再执行, 避免长时间加锁阻塞
void EventLoop::doPendingFunctors()
{
  // 用于swap回调列表
  std::vector<Functor> functors;
  // FIXME_hqb 状态标志的作用是什么
  callingPendingFunctors_ = true;

  // 交换出来，不会阻塞其他线程调用queueInLoop()
  // 避免了死锁, Functor可能会再调用queueInLoop(), 就会在这里死锁
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

// 输出下活跃的通道对应的事件，用于调试
void EventLoop::printActiveChannels() const
{
  for (const Channel* channel : activeChannels_)
  {
    LOG_TRACE << "{" << channel->reventsToString() << "} ";
  }
}

