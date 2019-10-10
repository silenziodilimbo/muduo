// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_CHANNEL_H
#define MUDUO_NET_CHANNEL_H

#include <muduo/base/noncopyable.h>
#include <muduo/base/Timestamp.h>

#include <functional>
#include <memory>

namespace muduo
{
namespace net
{

class EventLoop;

///
/// A selectable I/O channel.
///
/// This class doesn't own the file descriptor.
/// The file descriptor could be a socket,
/// an eventfd, a timerfd, or a signalfd
// IO事件回调的分发器dispatcher
// 通过它可以把其他一些现成的网络库融入muduo的event loop中
class Channel : noncopyable
{
 public:
  typedef std::function<void()> EventCallback; // 事件回调
  typedef std::function<void(Timestamp)> ReadEventCallback; // 读事件回调，带有事件参数

  Channel(EventLoop* loop, int fd);
  ~Channel();

  // 当通道产生事件时, EventLoop首先调用的方法
  void handleEvent(Timestamp receiveTime);

  // 根据revents_的值分别调用不同的用户回调
  void setReadCallback(ReadEventCallback cb)
  { readCallback_ = std::move(cb); }
  void setWriteCallback(EventCallback cb)
  { writeCallback_ = std::move(cb); }
  void setCloseCallback(EventCallback cb)
  { closeCallback_ = std::move(cb); }
  void setErrorCallback(EventCallback cb)
  { errorCallback_ = std::move(cb); }

  /// Tie this channel to the owner object managed by shared_ptr,
  /// prevent the owner object being destroyed in handleEvent.
  // tie此方法是防止Channel类还在执行，上层调用导致 
  // Channel提前释放而出现的异常问题，下文会详细解释。
  void tie(const std::shared_ptr<void>&);

  int fd() const { return fd_; }
  int events() const { return events_; }
  void set_revents(int revt) { revents_ = revt; } // used by pollers
  // int revents() const { return revents_; }
  // 检查当前Channel是否未处理任何事件
  bool isNoneEvent() const { return events_ == kNoneEvent; }

  // 使能读，并更新通道信息
  void enableReading() { events_ |= kReadEvent; update(); }
  void disableReading() { events_ &= ~kReadEvent; update(); }
  void enableWriting() { events_ |= kWriteEvent; update(); }
  void disableWriting() { events_ &= ~kWriteEvent; update(); }
  void disableAll() { events_ = kNoneEvent; update(); }
  bool isWriting() const { return events_ & kWriteEvent; }
  bool isReading() const { return events_ & kReadEvent; }

  // for Poller
  // 每个Channel在Poller管理时都有一个index
  int index() { return index_; }
  void set_index(int idx) { index_ = idx; }

  // for debug
  // 事件信息转换，便于调试
  string reventsToString() const;
  string eventsToString() const;

  // 是否输出POLLHUP挂起日志
  void doNotLogHup() { logHup_ = false; }

  EventLoop* ownerLoop() { return loop_; }
  void remove();

 private:
  static string eventsToString(int fd, int ev);

  void update();
  // 事件处理方法类
  // handleEvent调用, 具体的分发逻辑
  void handleEventWithGuard(Timestamp receiveTime);

  static const int kNoneEvent;
  static const int kReadEvent;
  static const int kWriteEvent;

  EventLoop* loop_; // 保存了所有者loop_, 可以向它添加/删除当前Channel事件
  const int  fd_; // Channel实际处理的fd
  int        events_; // IO事件的事件类型, 由用户设置
  int        revents_; // 目前活动的事件, 由EventLoop/Poller设置; it's the received event types of epoll or poll
  int        index_; // used by Poller.
  bool       logHup_;

  std::weak_ptr<void> tie_; // 用于tie()方法
  bool tied_;
  bool eventHandling_; // 表示当前是否正在处理事件
  bool addedToLoop_;
  ReadEventCallback readCallback_;
  EventCallback writeCallback_;
  EventCallback closeCallback_;
  EventCallback errorCallback_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_CHANNEL_H
