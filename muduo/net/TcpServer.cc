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
  // ��ֵ������Ϣ������
    ipPort_(listenAddr.toIpPort()),
    name_(nameArg),
  // �½�һ��Acceptor, ���𱻶���������, �����ӵ�ʱ��ᴴ��Connection
    acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)),
  // ����һ���̳߳�
    threadPool_(new EventLoopThreadPool(loop, name_)),
  // ����Ĭ�ϵ����ӻص�����Ϣ�ص�
    connectionCallback_(defaultConnectionCallback),
    messageCallback_(defaultMessageCallback),
  // ����״̬Ĭ��false����һ������IDĬ��1
    nextConnId_(1)
{
  // �������ӵ���ʱ������TcpServer�������ӻص�����
  acceptor_->setNewConnectionCallback(
      std::bind(&TcpServer::newConnection, this, _1, _2));
}

// �����Ĺ��̾����ͷ�TcpConnection
// KVӳ�� typedef std::map<string, TcpConnectionPtr> ConnectionMap connections_;
TcpServer::~TcpServer()
{
  loop_->assertInLoopThread();
  LOG_TRACE << "TcpServer::~TcpServer [" << name_ << "] destructing";

  for (auto& item : connections_)
  {
    TcpConnectionPtr conn(item.second);
    // ���ü�һ
    item.second.reset();
    // ����ÿ��TcpConnection::conectDestroyed()ȥ���ٶ�Ӧ��Channel��
    conn->getLoop()->runInLoop(
      std::bind(&TcpConnection::connectDestroyed, conn));
  }
}

// ����EventLoopʹ�õ��̳߳ظ���
void TcpServer::setThreadNum(int numThreads)
{
  assert(0 <= numThreads);
  threadPool_->setThreadNum(numThreads);
}

// ����TcpServer����
void TcpServer::start()
{
  if (started_.getAndSet(1) == 0)
  {
    threadPool_->start(threadInitCallback_);

    assert(!acceptor_->listenning());
    // �����½������ļ���
    loop_->runInLoop(
        std::bind(&Acceptor::listen, get_pointer(acceptor_)));
  }
}

// �����ӵ���ʱ���õĴ���ص�
// ����TcpConnection����conn
void TcpServer::newConnection(int sockfd, const InetAddress& peerAddr)
{
  loop_->assertInLoopThread();
  // �����ӵ���ʱ��Ҫ���̳߳���ȥһ��EventLoop���µ�����ʹ��
  // round-robin�㷨��ѡ
  EventLoop* ioLoop = threadPool_->getNextLoop();
  // ��֯�������ӵ�����TcpServerName:�˿�#ID��
  // Ĭ�Ͻ���һ��ID�ż�1
  char buf[64];
  snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
  ++nextConnId_;
  // TcpConnection������, ��key
  string connName = name_ + buf;

  LOG_INFO << "TcpServer::newConnection [" << name_
           << "] - new connection [" << connName
           << "] from " << peerAddr.toIpPort();
  InetAddress localAddr(sockets::getLocalAddr(sockfd));

  // FIXME poll with zero timeout to double confirm the new connection
  // FIXME use make_shared if necessary

  // �ؼ�����

  // ����һ���µ�TcpConnection
  // ��ʼ��
  // �󶨻ص�


  // ���������:
  // Loop ���loop���̳߳��е�һ��
  // �����ӵ�����
  // ����socket
  // ��ǰ�����ַ
  // Զ�����ӵ�ַ
  TcpConnectionPtr conn(new TcpConnection(ioLoop,
                                          connName,
                                          sockfd,
                                          localAddr,
                                          peerAddr));
  // ���浱ǰ����
  // TcpConnection������, ��key
  connections_[connName] = conn;
  // �������ɻص�
  // �������ӻص������ӶϿ��͹رն�����ã�
  conn->setConnectionCallback(connectionCallback_);
  // ��Ϣ�ص�
  conn->setMessageCallback(messageCallback_);
  conn->setWriteCompleteCallback(writeCompleteCallback_);
  // ���ùرջص����Ƴ���Ӧ��TcpConnection
  conn->setCloseCallback(
      std::bind(&TcpServer::removeConnection, this, _1)); // FIXME: unsafe
  
  // �������ӽ�������(��ʼ��״̬������Channdel��ʼ���� )
  // ioLoop������: �����ӵ���ʱ��Ҫ���̳߳���ȥһ��EventLoop���µ�����ʹ��
  ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn)
{
  // FIXME: unsafe
  loop_->runInLoop(std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

// ��ÿ��TcpConnection�����߳��ڲ�ȥ���ٵ�ǰ����
void TcpServer::removeConnectionInLoop(const TcpConnectionPtr& conn)
{
  loop_->assertInLoopThread();
  LOG_INFO << "TcpServer::removeConnectionInLoop [" << name_
           << "] - connection " << conn->name();
  size_t n = connections_.erase(conn->name());
  (void)n;
  assert(n == 1);
  EventLoop* ioLoop = conn->getLoop();
  // ������std::bind��TcpConnection�������ڳ�������connectDestroyed()��ʱ��
  ioLoop->queueInLoop(
      std::bind(&TcpConnection::connectDestroyed, conn));
}

