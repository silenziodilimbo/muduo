// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_CONNECTOR_H
#define MUDUO_NET_CONNECTOR_H

#include <muduo/base/noncopyable.h>
#include <muduo/net/InetAddress.h>

#include <functional>
#include <memory>

namespace muduo
{
namespace net
{

class Channel;
class EventLoop;

// ��ֻ����socket������, �����𴴽�TcpConnection
class Connector : noncopyable,
                  public std::enable_shared_from_this<Connector>
{
 public:
  typedef std::function<void (int sockfd)> NewConnectionCallback;

  Connector(EventLoop* loop, const InetAddress& serverAddr);
  ~Connector();

  // �����ӻص�
  void setNewConnectionCallback(const NewConnectionCallback& cb)
  { newConnectionCallback_ = cb; }

  void start();  // can be called in any thread
  void restart();  // must be called in loop thread
  void stop();  // can be called in any thread

  // ����˵�ַ
  const InetAddress& serverAddress() const { return serverAddr_; }

 private:
   // ��������״̬  
  enum States { kDisconnected, kConnecting, kConnected };
  // �������ʱ��30s
  static const int kMaxRetryDelayMs = 30*1000;
  // ��ʼ����ʱ��500ms
  static const int kInitRetryDelayMs = 500;

  // ��ǰ����״̬
  void setState(States s) { state_ = s; }
  // ��ʼ����
  void startInLoop();
  // ֹͣ����
  void stopInLoop();
  // ����
  void connect();
  // ��������
  void connecting(int sockfd);
  // д�������
  void handleWrite();
  //�������
  void handleError();
  //����
  void retry(int sockfd);
  //�Ƴ����ͷ�Channel
  int removeAndResetChannel();
  //�ͷ�Channel
  void resetChannel();

    //IO�������
  EventLoop* loop_;
  //����˵�ַ
  InetAddress serverAddr_;
  //����״̬λ
  bool connect_; // atomic
  States state_;  // FIXME: use atomic variable
  //�������ӷ���˵�Channel
  std::unique_ptr<Channel> channel_;
  //���ӳɹ��Ժ�Ҫ���õĻص�
  NewConnectionCallback newConnectionCallback_;
  //�������
  int retryDelayMs_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_CONNECTOR_H
