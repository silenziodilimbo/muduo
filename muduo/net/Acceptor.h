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
  //用socketfd创建的, 当fd可读的时候,即有了新连接的时候, 会调用newConnectionCallback_回调，创建一个TcpConnection
  //步骤如下:
  //channel的构造函数中，指明了事件。acceptChannel_(loop, acceptSocket_.fd()),
  //这个事件就是socketfd（地址）的可读事件，也就是新连接到来，
  //channel注册在所属事件驱动循环（EventLoop）中的Poller上。
  //当loop()函数监听到通道acceptChannel_有事件到来，即listen套接字可读时，即新连接到来
  //调用这个通道的readCallback，即acceptChannel_->handleEvent()
  //即Acceptor::handleRead()
  //handleRead()函数中又调用了accept()接收客户端的请求
  //连接建立后，会调用注册的回调函数newConnectionCallback_()
  //这个回调中, 会创建一个TcpConnection, 里面是一个channel（用了connfd），还有一个socketfd（用了connfd）
  //这个channel绑定了用户级别的读写回调
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
