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
// IO�¼��ص��ķַ���dispatcher
// ͨ�������԰�����һЩ�ֳɵ����������muduo��event loop��
class Channel : noncopyable
{
 public:
  typedef std::function<void()> EventCallback; // �¼��ص�
  typedef std::function<void(Timestamp)> ReadEventCallback; // ���¼��ص��������¼�����

  Channel(EventLoop* loop, int fd);
  ~Channel();

  // ��ͨ�������¼�ʱ, EventLoop���ȵ��õķ���
  void handleEvent(Timestamp receiveTime);

  // ����revents_��ֵ�ֱ���ò�ͬ���û��ص�
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
  // tie�˷����Ƿ�ֹChannel�໹��ִ�У��ϲ���õ��� 
  // Channel��ǰ�ͷŶ����ֵ��쳣���⣬���Ļ���ϸ���͡�
  void tie(const std::shared_ptr<void>&);

  int fd() const { return fd_; }
  int events() const { return events_; }
  void set_revents(int revt) { revents_ = revt; } // used by pollers
  // int revents() const { return revents_; }
  // ��鵱ǰChannel�Ƿ�δ�����κ��¼�
  bool isNoneEvent() const { return events_ == kNoneEvent; }

  // ʹ�ܶ���������ͨ����Ϣ
  void enableReading() { events_ |= kReadEvent; update(); }
  void disableReading() { events_ &= ~kReadEvent; update(); }
  void enableWriting() { events_ |= kWriteEvent; update(); }
  void disableWriting() { events_ &= ~kWriteEvent; update(); }
  void disableAll() { events_ = kNoneEvent; update(); }
  bool isWriting() const { return events_ & kWriteEvent; }
  bool isReading() const { return events_ & kReadEvent; }

  // for Poller
  // ÿ��Channel��Poller����ʱ����һ��index
  int index() { return index_; }
  void set_index(int idx) { index_ = idx; }

  // for debug
  // �¼���Ϣת�������ڵ���
  string reventsToString() const;
  string eventsToString() const;

  // �Ƿ����POLLHUP������־
  void doNotLogHup() { logHup_ = false; }

  EventLoop* ownerLoop() { return loop_; }
  void remove();

 private:
  static string eventsToString(int fd, int ev);

  void update();
  // �¼���������
  // handleEvent����, ����ķַ��߼�
  void handleEventWithGuard(Timestamp receiveTime);

  static const int kNoneEvent;
  static const int kReadEvent;
  static const int kWriteEvent;

  EventLoop* loop_; // ������������loop_, �����������/ɾ����ǰChannel�¼�
  const int  fd_; // Channelʵ�ʴ����fd
  int        events_; // IO�¼����¼�����, ���û�����
  int        revents_; // Ŀǰ����¼�, ��EventLoop/Poller����; it's the received event types of epoll or poll
  int        index_; // used by Poller.
  bool       logHup_;

  std::weak_ptr<void> tie_; // ����tie()����
  bool tied_;
  bool eventHandling_; // ��ʾ��ǰ�Ƿ����ڴ����¼�
  bool addedToLoop_;
  ReadEventCallback readCallback_;
  EventCallback writeCallback_;
  EventCallback closeCallback_;
  EventCallback errorCallback_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_CHANNEL_H
