// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/base/Logging.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>

#include <sstream>

#include <poll.h>

using namespace muduo;
using namespace muduo::net;


// 读写事件值初始化
const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = POLLIN | POLLPRI;
const int Channel::kWriteEvent = POLLOUT;

Channel::Channel(EventLoop* loop, int fd__)
  : loop_(loop),
    fd_(fd__),
    events_(0),
    revents_(0),
    index_(-1),
    logHup_(true),
    tied_(false),
    eventHandling_(false),
    addedToLoop_(false)
{
}

Channel::~Channel()
{
  // 它不需要关闭fd

  // 析构时，如果当前Channel事件还在处理，则异常。
  // channel是由TcpConnetion创建的哦!
  assert(!eventHandling_);
  assert(!addedToLoop_);
  if (loop_->isInLoopThread())
  {
    assert(!loop_->hasChannel(this));
  }
}

void Channel::tie(const std::shared_ptr<void>& obj)
{

  //捆绑Channel的拥有者，防止Channel还在使用时，
  //拥有者将Channel析构了
  tie_ = obj;
  tied_ = true;
}

// 由enable/disable reading/Writing调用
// 实际上是调用了EventLoop::updateChannel来更新自己, 即channel
// 这个函数调用了Poller::updateChannel方法, 来更新channel
// 如果是新的Channel (没有index)
// 创建一个pollfd来管理它, 包括fd/events/revents
// 把pollfd放到队列中
// 生成一个channel的index, 赋值给channel, 用作标记
// 把channel放到队列中
// 如果不是新的channel (已经有了index)
// 取出pfd来进行更新
// 如果某个Channel暂时不关心任何事件, 就置为-1, 让poll忽略此项
void Channel::update()
{

  // 调用EventLoop更新此通道信息
  addedToLoop_ = true;
  loop_->updateChannel(this);
}

// 当前无任何事件的情况下，
// 移除Channel管理队列中本Channel。
// Eventloop析构的时候， 会调用wakeupChannel::disable和remove
// Acceptor析构的时候会调用
// Connector释放的时候会调用
// TcpConnection断开的时候会调用
// 实际上是调用了EventLoop::removeChannel
// 这个函数调用了Poller::removeChannel方法
// 从Channel中取出index
// 列表中删除fd
// 列表中删除channel
void Channel::remove()
{
  assert(isNoneEvent());
  addedToLoop_ = false;
  loop_->removeChannel(this);
}

// 由EventLoop::loop()调用
// 每次循环轮流调用所有activeChannel的handleEvent
// 实际调用了handleEventWithGuard
// 事件处理， 检测事件类型，调用相应的Read / Write / Error回调
void Channel::handleEvent(Timestamp receiveTime)
{
  std::shared_ptr<void> guard;
  if (tied_)
  {
    // 捆绑了Channel的拥有者的处理方式。
    // tie_用的是boost::weak_ptr，所以
    // 要先lock获取下，然后判断是否可用。
    guard = tie_.lock();
    if (guard)
    {
      handleEventWithGuard(receiveTime);
    }
  }
  else
  {
    handleEventWithGuard(receiveTime);
  }
}


// handleEvent调用， 事件处理的真实逻辑
// 由EventLoop::loop()调用
// 每次循环轮流调用所有activeChannel的handleEvent
// 处理当前channel里面的各种事件
void Channel::handleEventWithGuard(Timestamp receiveTime)
{

  // 事件处理时，设置下此状态，
  // Channel析构时，用到此状态 
  eventHandling_ = true;
  LOG_TRACE << reventsToString();
  
  // revents_是Poller设置的
  // 当有fd上有事件发生的时候， 操作系统会给出一个值， Poller调用set_revents赋值到Channel上
  // 这样Channel就能通过revents来判断了
  if ((revents_ & POLLHUP) && !(revents_ & POLLIN))
  {
    // 文件描述符挂起，并且不是读事件
    // POLLHUP 描述符挂起，比如管道的写端被关闭后，读端描述符将收到此事件
    if (logHup_)
    {
      LOG_WARN << "fd = " << fd_ << " Channel::handle_event() POLLHUP";
    }
    if (closeCallback_) closeCallback_();
  }

  if (revents_ & POLLNVAL)
  {
        //指定的文件描述符非法，
    //输出日志便于errorCallback_区分错误。
    LOG_WARN << "fd = " << fd_ << " Channel::handle_event() POLLNVAL";
  }

  if (revents_ & (POLLERR | POLLNVAL))
  {
    //POLLERR 指定的描述符发生错误。
    //POLLNVAL 指定的描述符非法(描述符未打开)
    if (errorCallback_) errorCallback_();
  }
  if (revents_ & (POLLIN | POLLPRI | POLLRDHUP))
  {
    //POLLIN 普通数据可读(普通数据+优先级数据)
    //POLLPRI 紧急数据(优先级数据)
    //POLLRDHUP TCP连接被对方关闭，或者对方关闭了写操作，由GNU引入。
    if (readCallback_) readCallback_(receiveTime);
  }
  if (revents_ & POLLOUT)
  {
    //数据可写
    if (writeCallback_) writeCallback_();
  }
  eventHandling_ = false;
}

string Channel::reventsToString() const
{
  return eventsToString(fd_, revents_);
}

string Channel::eventsToString() const
{
  return eventsToString(fd_, events_);
}

// 此方法就是将对应的事件转换为字符串输出，便于调试。
string Channel::eventsToString(int fd, int ev)
{
  std::ostringstream oss;
  oss << fd << ": ";
  if (ev & POLLIN)
    oss << "IN ";
  if (ev & POLLPRI)
    oss << "PRI ";
  if (ev & POLLOUT)
    oss << "OUT ";
  if (ev & POLLHUP)
    oss << "HUP ";
  if (ev & POLLRDHUP)
    oss << "RDHUP ";
  if (ev & POLLERR)
    oss << "ERR ";
  if (ev & POLLNVAL)
    oss << "NVAL ";

  return oss.str();
}
