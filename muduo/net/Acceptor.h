// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_ACCEPTOR_H
#define MUDUO_NET_ACCEPTOR_H

#include <functional>

#include <muduo/net/Channel.h>
#include <muduo/net/Socket.h>

namespace muduo
{
namespace net
{

class EventLoop;
class InetAddress;

///
/// Acceptor of incoming TCP connections.
/// Acceptor用于接受（accept）客户端的连接，通过设置回调函数通知使用者。
/// 它只在muduo网络库内部的TcpServer使用，
/// 由TcpServer控制它的生命期。
/// 实际上，Acceptor只是对Channel的封装，通过Channel关注listenfd的readable可读事件，并设置好回调函数就可以了
///
class Acceptor : noncopyable
{
 public:
   // accept后调用的函数对象
  typedef std::function<void (int sockfd, const InetAddress&)> NewConnectionCallback;

  Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport);
  ~Acceptor();

  void setNewConnectionCallback(const NewConnectionCallback& cb)
  { newConnectionCallback_ = cb; }

  bool listenning() const { return listenning_; }
  void listen();

 private:
  void handleRead();

  EventLoop* loop_;
  // 初始化创建sockt fd, Socket是个RAII型，析构时自动close文件描述符
  Socket acceptSocket_;
  // 初始化channel, 设置监听套接字的readable事件以及回调函数
  Channel acceptChannel_;
  NewConnectionCallback newConnectionCallback_;
  // 是否正在监听状态
  bool listenning_;
  int idleFd_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_ACCEPTOR_H
