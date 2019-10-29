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
/// Acceptor���ڽ��ܣ�accept���ͻ��˵����ӣ�ͨ�����ûص�����֪ͨʹ���ߡ�
/// ��ֻ��muduo������ڲ���TcpServerʹ�ã�
/// ��TcpServer�������������ڡ�
/// ʵ���ϣ�Acceptorֻ�Ƕ�Channel�ķ�װ��ͨ��Channel��עlistenfd��readable�ɶ��¼��������úûص������Ϳ�����
///
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
  // ��ʼ������sockt fd, Socket�Ǹ�RAII�ͣ�����ʱ�Զ�close�ļ�������
  Socket acceptSocket_;
  // ��ʼ��channel, ���ü����׽��ֵ�readable�¼��Լ��ص�����
  Channel acceptChannel_;
  NewConnectionCallback newConnectionCallback_;
  // �Ƿ����ڼ���״̬
  bool listenning_;
  int idleFd_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_ACCEPTOR_H
