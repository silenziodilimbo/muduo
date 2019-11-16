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

//Acceptor of incoming TCP connections.
//Acceptor用于接受(accept)客户端的连接，通过设置回调函数通知使用者。
//是TcpServer的成员变量
//由TcpServer控制它的生命期。
//实际上，Acceptor只是对Channel的封装，通过Channel关注listenfd的readable可读事件，并设置好回调函数就可以了
//Acceptor是被动接收连接(存在于TcpServer中), Connector是主动接收连接(存在于TcpClient中)
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
  //初始化创建socketfd, socketfd是个RAII，析构时自动close文件描述符
  //用listenAddr来初始化这个socket
  //这个socket是listening socket, 即 server socket
  //createNonblockingOrDie可以创建非阻塞socket, linux来完成的
  Socket acceptSocket_;
  //channel
  //用socketfd创建的, 当fd可读的时候, 会调用newConnectionCallback_回调
  //步骤如下:
  //1. TcpServer会调用它的listen()接口
  //把channel注册自己到所属事件驱动循环（EventLoop）中的Poller上
  //2. 当这个channel变得可读的时候, 会执行回调Acceptor::handleRead
  //3. 在handlerRead中, 会执行由TcpServer Set的newConnectionCallback_回调
  Channel acceptChannel_;
  // 回调, 在TcpServer的构造函数中会初始化它
  NewConnectionCallback newConnectionCallback_;
  // 是否正在监听状态
  bool listenning_;
  int idleFd_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_ACCEPTOR_H
