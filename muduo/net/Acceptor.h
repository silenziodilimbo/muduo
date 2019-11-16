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
//Acceptor���ڽ���(accept)�ͻ��˵����ӣ�ͨ�����ûص�����֪ͨʹ���ߡ�
//��TcpServer�ĳ�Ա����
//��TcpServer�������������ڡ�
//ʵ���ϣ�Acceptorֻ�Ƕ�Channel�ķ�װ��ͨ��Channel��עlistenfd��readable�ɶ��¼��������úûص������Ϳ�����
//Acceptor�Ǳ�����������(������TcpServer��), Connector��������������(������TcpClient��)
class Acceptor : noncopyable
{
 public:
   // accept����õĺ�������
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
  //��ʼ������socketfd, socketfd�Ǹ�RAII������ʱ�Զ�close�ļ�������
  //��listenAddr����ʼ�����socket
  //���socket��listening socket, �� server socket
  //createNonblockingOrDie���Դ���������socket, linux����ɵ�
  Socket acceptSocket_;
  //channel
  //��socketfd������, ��fd�ɶ���ʱ��, �����newConnectionCallback_�ص�
  //��������:
  //1. TcpServer���������listen()�ӿ�
  //��channelע���Լ��������¼�����ѭ����EventLoop���е�Poller��
  //2. �����channel��ÿɶ���ʱ��, ��ִ�лص�Acceptor::handleRead
  //3. ��handlerRead��, ��ִ����TcpServer Set��newConnectionCallback_�ص�
  Channel acceptChannel_;
  // �ص�, ��TcpServer�Ĺ��캯���л��ʼ����
  NewConnectionCallback newConnectionCallback_;
  // �Ƿ����ڼ���״̬
  bool listenning_;
  int idleFd_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_ACCEPTOR_H
