// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/Acceptor.h>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/SocketsOps.h>

#include <errno.h>
#include <fcntl.h>
//#include <sys/types.h>
//#include <sys/stat.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

Acceptor::Acceptor(EventLoop* loop, const InetAddress& listenAddr, bool reuseport)
  : loop_(loop),
    acceptSocket_(sockets::createNonblockingOrDie(listenAddr.family())),
    acceptChannel_(loop, acceptSocket_.fd()),
    listenning_(false),
    idleFd_(::open("/dev/null", O_RDONLY | O_CLOEXEC))
{
  assert(idleFd_ >= 0);
  acceptSocket_.setReuseAddr(true);
  acceptSocket_.setReusePort(reuseport);
  // socket的bind工作
  acceptSocket_.bindAddress(listenAddr);
  // 当fd可读时调用回调函数hanleRead
  acceptChannel_.setReadCallback(
      std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor()
{
  // 将其从poller监听集合中移除，此时为kDeleted状态
  acceptChannel_.disableAll();
  // Poller会持有Channel的裸指针，所以需要将该Channel从Poller中删除，避免Channel析构后，Poller持有空悬指针。，此时为kNew状态
  acceptChannel_.remove();
  ::close(idleFd_);
}

// TcpServer::start()调用
// 启动监听套接字
void Acceptor::listen()
{
  loop_->assertInLoopThread();
  listenning_ = true;
  // listen系统调用
  acceptSocket_.listen();
  // 通过enableReading将accept_channel加到poll数组中
  //Channel注册自己到所属事件驱动循环（EventLoop）中的Poller上
  acceptChannel_.enableReading();
}

void Acceptor::handleRead()
{
  loop_->assertInLoopThread();
  // InetAddress是对struct sockaddr_in的简单封装, 能自动转换字节序
  InetAddress peerAddr;
  //FIXME loop until no more
  // 接受新连接
  // 这里是真正接收连接
  // 接受客户端的连接，同时设置连接socket为非阻塞方式。
  // 实际上调用了TcpServer::newConnection
  int connfd = acceptSocket_.accept(&peerAddr);
  if (connfd >= 0)
  {
    // string hostport = peerAddr.toIpPort();
    // LOG_TRACE << "Accepts of " << hostport;
    // 执行用户回调
    // 是TcpServer通过acceptor.setNewConnectionCallback(cb)来设置的
    // 即TcpServer::newConnection
    if (newConnectionCallback_)
    {
      // 将新连接信息传送到回调函数中, 即TcpServer::newConnection
      // 在TcpServer的构造函数中, 会创建一个端口的acceptor_, 并set新连接回调
      // 回调进一步创建TcpConnectionPtr conn
      newConnectionCallback_(connfd, peerAddr);
    }
    // 没有回调函数则关闭client对应的fd
    else
    {
      sockets::close(connfd);
    }
  }
  else
  {
    LOG_SYSERR << "in Acceptor::handleRead";
    // Read the section named "The special problem of
    // accept()ing when you can't" in libev's doc.
    // By Marc Lehmann, author of libev.
    if (errno == EMFILE)
    {
      ::close(idleFd_);
      idleFd_ = ::accept(acceptSocket_.fd(), NULL, NULL);
      ::close(idleFd_);
      idleFd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    }
  }
}

