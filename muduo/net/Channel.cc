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


// ��д�¼�ֵ��ʼ��
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
  // ������Ҫ�ر�fd

  //����ʱ�������ǰChannel
  //�¼����ڴ������쳣��
  assert(!eventHandling_);
  assert(!addedToLoop_);
  if (loop_->isInLoopThread())
  {
    assert(!loop_->hasChannel(this));
  }
}

void Channel::tie(const std::shared_ptr<void>& obj)
{

  //����Channel��ӵ���ߣ���ֹChannel����ʹ��ʱ��
  //ӵ���߽�Channel������
  tie_ = obj;
  tied_ = true;
}

void Channel::update()
{

  // ����EventLoop���´�ͨ����Ϣ
  addedToLoop_ = true;
  loop_->updateChannel(this);
}

void Channel::remove()
{

  //��ǰ���κ��¼�������£�
  //�Ƴ�Channel��������б�Channel��
  assert(isNoneEvent());
  addedToLoop_ = false;
  loop_->removeChannel(this);
}

// ��EventLoop����
// ����revents_��ֵ�ֱ���ò�ͬ���û��ص�
// ����ķַ��߼���handleEventWithGuard��
void Channel::handleEvent(Timestamp receiveTime)
{
  std::shared_ptr<void> guard;
  if (tied_)
  {
    // ������Channel��ӵ���ߵĴ���ʽ��
    // tie_�õ���boost::weak_ptr������
    // Ҫ��lock��ȡ�£�Ȼ���ж��Ƿ���á�
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

void Channel::handleEventWithGuard(Timestamp receiveTime)
{

  //�¼�����ʱ�������´�״̬��
  //Channel����ʱ���õ���״̬ 
  eventHandling_ = true;
  LOG_TRACE << reventsToString();
  if ((revents_ & POLLHUP) && !(revents_ & POLLIN))
  {
    //�ļ����������𣬲��Ҳ��Ƕ��¼�
    //POLLHUP ���������𣬱���ܵ���д�˱��رպ󣬶������������յ����¼�
    if (logHup_)
    {
      LOG_WARN << "fd = " << fd_ << " Channel::handle_event() POLLHUP";
    }
    if (closeCallback_) closeCallback_();
  }

  if (revents_ & POLLNVAL)
  {
        //ָ�����ļ��������Ƿ���
    //�����־����errorCallback_���ִ���
    LOG_WARN << "fd = " << fd_ << " Channel::handle_event() POLLNVAL";
  }

  if (revents_ & (POLLERR | POLLNVAL))
  {
    //POLLERR ָ������������������
    //POLLNVAL ָ�����������Ƿ�(������δ��)
    if (errorCallback_) errorCallback_();
  }
  if (revents_ & (POLLIN | POLLPRI | POLLRDHUP))
  {
    //POLLIN ��ͨ���ݿɶ�(��ͨ����+���ȼ�����)
    //POLLPRI ��������(���ȼ�����)
    //POLLRDHUP TCP���ӱ��Է��رգ����߶Է��ر���д��������GNU���롣
    if (readCallback_) readCallback_(receiveTime);
  }
  if (revents_ & POLLOUT)
  {
    //���ݿ�д
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

//�˷������ǽ���Ӧ���¼�ת��Ϊ�ַ�����������ڵ��ԡ�
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
