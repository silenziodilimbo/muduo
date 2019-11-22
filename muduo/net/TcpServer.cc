// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/TcpServer.h>

#include <muduo/base/Logging.h>
#include <muduo/net/Acceptor.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/EventLoopThreadPool.h>
#include <muduo/net/SocketsOps.h>

#include <stdio.h>  // snprintf

using namespace muduo;
using namespace muduo::net;

TcpServer::TcpServer(EventLoop* loop,
                     const InetAddress& listenAddr,
                     const string& nameArg,
                     Option option)
  : loop_(CHECK_NOTNULL(loop)),
  // 赋值网络信息和名称
    ipPort_(listenAddr.toIpPort()),
    name_(nameArg),
  // 新建一个Acceptor, 负责被动接收连接, 有连接的时候会创建Connection
    acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),
  // 创建一个线程池
    threadPool_(new EventLoopThreadPool(loop, name_)),
  // 设置默认的连接回调和消息回调
    connectionCallback_(defaultConnectionCallback),
    messageCallback_(defaultMessageCallback),
  // 启动状态默认false、下一个连接ID默认1
    nextConnId_(1)
{
  // 当有连接到来时，设置TcpServer的新连接回调函数
  acceptor_->setNewConnectionCallback(
      std::bind(&TcpServer::newConnection, this, _1, _2));
}

// 析构的过程就是释放TcpConnection
// KV映射 typedef std::map<string, TcpConnectionPtr> ConnectionMap connections_;
TcpServer::~TcpServer()
{
  loop_->assertInLoopThread();
  LOG_TRACE << "TcpServer::~TcpServer [" << name_ << "] destructing";

  for (auto& item : connections_)
  {
    TcpConnectionPtr conn(item.second);
    // 引用减一
    item.second.reset();
    // 调用每个TcpConnection::conectDestroyed()去销毁对应的Channel。
    conn->getLoop()->runInLoop(
      std::bind(&TcpConnection::connectDestroyed, conn));
  }
}

// 设置EventLoop使用的线程池个数
void TcpServer::setThreadNum(int numThreads)
{
  assert(0 <= numThreads);
  threadPool_->setThreadNum(numThreads);
}

// 开启TcpServer服务
void TcpServer::start()
{
  if (started_.getAndSet(1) == 0)
  {
    threadPool_->start(threadInitCallback_);

    assert(!acceptor_->listenning());
    // 开启下接收器的监听
    loop_->runInLoop(
        std::bind(&Acceptor::listen, get_pointer(acceptor_)));
  }
}

// 新连接到达时调用的处理回调
// 创建TcpConnection对象conn
void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr)
{
  loop_->assertInLoopThread();
  // 新连接到来时，要从线程池中去一个EventLoop给新的连接使用
  // round-robin算法来选
  EventLoop* ioLoop = threadPool_->getNextLoop();
  // 组织下新连接的名称TcpServerName:端口#ID号
  // 默认将下一个ID号加1
  char buf[64];
  snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
  ++nextConnId_;
  // TcpConnection的名字, 做key
  string connName = name_ + buf;

  LOG_INFO << "TcpServer::newConnection [" << name_
           << "] - new connection [" << connName
           << "] from " << peerAddr.toIpPort();
  InetAddress localAddr(sockets::getLocalAddr(sockfd));

  // FIXME poll with zero timeout to double confirm the new connection
  // FIXME use make_shared if necessary

  // 关键内容

  // 创建一个新的TcpConnection
  // 初始化
  // 绑定回调


  // 传入参数有:
  // Loop 这个loop是线程池中的一个
  // 新连接的名称
  // 连接socket
  // 当前服务地址
  // 远程连接地址
  TcpConnectionPtr conn(new TcpConnection(ioLoop,
                                          connName,
                                          sockfd,
                                          localAddr,
                                          peerAddr));
  // 保存当前连接
  // TcpConnection的名字, 做key
  connections_[connName] = conn;
  // 设置若干回调
  // 设置连接回调（连接断开和关闭都会调用）
  conn->setConnectionCallback(connectionCallback_);
  // 消息回调
  conn->setMessageCallback(messageCallback_);
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  // 设置关闭回调，移除对应的TcpConnection
  conn->setCloseCallback(
      std::bind(&TcpServer::removeConnection, this, _1)); // FIXME: unsafe
  
  // 调用连接建立方法(初始化状态，启动Channdel开始读等 )
  // ioLoop建立于: 新连接到来时，要从线程池中去一个EventLoop给新的连接使用
  ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn)
{
  // FIXME: unsafe
  loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

// 在每个TcpConnection所在线程内部去销毁当前连接
void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
  loop_->assertInLoopThread();
  LOG_INFO << "TcpServer::removeConnectionInLoop [" << name_
           << "] - connection " << conn->name();
  size_t n = connections_.erase(conn->name());
  (void)n;
  assert(n == 1);
  EventLoop* ioLoop = conn->getLoop();
  // 这里用std::bind让TcpConnection的生命期长到调用connectDestroyed()的时刻
  ioLoop->queueInLoop(
      std::bind(&TcpConnection::connectDestroyed, conn));
}

