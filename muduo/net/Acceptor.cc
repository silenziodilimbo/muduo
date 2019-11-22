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
  // socket��bind����
  acceptSocket_.bindAddress(listenAddr);
  // ��fd�ɶ�ʱ���ûص�����hanleRead
  acceptChannel_.setReadCallback(
      std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor()
{
  // �����poller�����������Ƴ�����ʱΪkDeleted״̬
  acceptChannel_.disableAll();
  // Poller�����Channel����ָ�룬������Ҫ����Channel��Poller��ɾ��������Channel������Poller���п���ָ�롣����ʱΪkNew״̬
  acceptChannel_.remove();
  ::close(idleFd_);
}

// TcpServer::start()����
// ���������׽���
void Acceptor::listen()
{
  loop_->assertInLoopThread();
  listenning_ = true;
  // listenϵͳ����
  acceptSocket_.listen();
  // ͨ��enableReading��accept_channel�ӵ�poll������
  //Channelע���Լ��������¼�����ѭ����EventLoop���е�Poller��
  acceptChannel_.enableReading();
}

void Acceptor::handleRead()
{
  loop_->assertInLoopThread();
  // InetAddress�Ƕ�struct sockaddr_in�ļ򵥷�װ, ���Զ�ת���ֽ���
  InetAddress peerAddr;
  //FIXME loop until no more
  // ����������
  // ������������������
  // ���ܿͻ��˵����ӣ�ͬʱ��������socketΪ��������ʽ��
  // ʵ���ϵ�����TcpServer::newConnection
  int connfd = acceptSocket_.accept(&peerAddr);
  if (connfd >= 0)
  {
    // string hostport = peerAddr.toIpPort();
    // LOG_TRACE << "Accepts of " << hostport;
    // ִ���û��ص�
    // ��TcpServerͨ��acceptor.setNewConnectionCallback(cb)�����õ�
    // ��TcpServer::newConnection
    if (newConnectionCallback_)
    {
      // ����������Ϣ���͵��ص�������, ��TcpServer::newConnection
      // ��TcpServer�Ĺ��캯����, �ᴴ��һ���˿ڵ�acceptor_, ��set�����ӻص�
      // �ص���һ������TcpConnectionPtr conn
      newConnectionCallback_(connfd, peerAddr);
    }
    // û�лص�������ر�client��Ӧ��fd
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

