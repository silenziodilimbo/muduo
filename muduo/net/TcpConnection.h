// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_TCPCONNECTION_H
#define MUDUO_NET_TCPCONNECTION_H

#include <muduo/base/noncopyable.h>
#include <muduo/base/StringPiece.h>
#include <muduo/base/Types.h>
#include <muduo/net/Callbacks.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/InetAddress.h>

#include <memory>

#include <boost/any.hpp>

// struct tcp_info is in <netinet/tcp.h>
struct tcp_info;

namespace muduo
{
namespace net
{

class Channel;
class EventLoop;
class Socket;

///
/// TCP connection, for both client and server usage.
///
/// This is an interface class, so don't expose too much details.
class TcpConnection : noncopyable,
                      public std::enable_shared_from_this<TcpConnection>
{
 public:
  /// Constructs a TcpConnection with a connected sockfd
  ///
  /// User should not create this object.
  TcpConnection(EventLoop* loop,
                const string& name,
                int sockfd,
                const InetAddress& localAddr,
                const InetAddress& peerAddr);
  ~TcpConnection();

  // ��ȡ��ǰTcpConnection���ڵ�EventLoop
  EventLoop* getLoop() const { return loop_; }
  // TcpConnection����
  const string& name() const { return name_; }
  // ��ǰ����˵�ַ
  const InetAddress& localAddress() const { return localAddr_; }
  // Զ�����ӿͻ��˵�ַ
  const InetAddress& peerAddress() const { return peerAddr_; }
  // ����Ƿ�����
  bool connected() const { return state_ == kConnected; }
  bool disconnected() const { return state_ == kDisconnected; }
  // return true if success.
  bool getTcpInfo(struct tcp_info*) const;
  string getTcpInfoString() const;

  // void send(string&& message); // C++11 // ��������
  // ��������
  // ����ֵ��void,��ζ���û����ع��ĵ���sendʱ�ɹ������˶����ֽ�,muduo�Ᵽ֤���͸��Է�
  // ������,�û�ֻҪsend(),�Ͳ�������,��ʹTCP���ʹ�������
  // �̰߳�ȫ,Դ�ֵ�,����߳�ͬʱ����sendҲ��������֯,���Ƕ���̵߳��Ⱥ�˳��ȷ��
  void send(const void* message, int len);
  void send(const StringPiece& message); // StringPiece��Google������ר�����ڴ����ַ���������class,������������const char*��const std::string&
  // void send(Buffer&& message); // C++11 ֱ������std::move ������ֵ���������⿽��
  void send(Buffer* message);  // this one will swap data ����Ϊָ��,������const����.��Ϊ��������ʹ��swap����Ч�ؽ�������,������ֵ����(�����Ǹ�)
  // �رգ�������һ���Ĵ����߼�
  void shutdown(); // NOT thread safe, no simultaneous calling
  // void shutdownAndForceCloseAfter(double seconds); // NOT thread safe, no simultaneous calling
  // ǿ�йر�, ���յ���handleClose()
  void forceClose();
  void forceCloseWithDelay(double seconds);
  // ����tcpNoDelay
  void setTcpNoDelay(bool on);
  // reading or not
  void startRead();
  void stopRead();
  bool isReading() const { return reading_; }; // NOT thread safe, may race with start/stopReadInLoop

  // �������ݡ�������ݿ������κ����ݣ���Ҫ������һ����ʱ�洢���á�
  void setContext(const boost::any& context)
  { context_ = context; }

  // ��ȡ���ݵ�����(��ȡ��ǰ���ݣ�һ���ڻص���ʹ��)
  const boost::any& getContext() const
  { return context_; }

  // ��ȡ���ݵĵ�ַ(��ȡ��ǰ���ݣ�һ���ڻص���ʹ��)
  boost::any* getMutableContext()
  { return &context_; }

  // �������ӻص���һ��������ʲô?
  // a�����ӵĽ��������ӵ����١������ر��¼�ʱ����ô˻ص���֪ͨ�ⲿ״̬��
  void setConnectionCallback(const ConnectionCallback& cb)
  { connectionCallback_ = cb; }

  // ������Ϣ�ص���һ����յ�����֮���ص��˷���
  void setMessageCallback(const MessageCallback& cb)
  { messageCallback_ = cb; }

  // д��ɻص���
  void setWriteCompleteCallback(const WriteCompleteCallback& cb)
  { writeCompleteCallback_ = cb; }

  // ���ø�ˮλ�ص���
  void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark)
  { highWaterMarkCallback_ = cb; highWaterMark_ = highWaterMark; }

  // ��ȡ����Buffer��ַ
  /// Advanced interface
  Buffer* inputBuffer()
  { return &inputBuffer_; }

  Buffer* outputBuffer()
  { return &outputBuffer_; }

  // �رջص�
  /// Internal use only.
  // ����ص��Ǹ�TcpServer��TcpClient�õ�
  // ֪ͨ�����Ƴ������е�TcpConnectionPtr
  // ���Ǹ���ͨ�û��õ�
  // ��ͨ�û�����ʹ��ConnectionCallback
  void setCloseCallback(const CloseCallback& cb)
  { closeCallback_ = cb; }

  // called when TcpServer accepts a new connection
  void connectEstablished();   // should be called only once
  // called when TcpServer has removed me from its map
  // TcpConnection����ǰ�����õ�һ������
  // ��֪ͨ�û������ѶϿ�
  void connectDestroyed();  // should be called only once

 private:
  enum StateE { kDisconnected, kConnecting, kConnected, kDisconnecting };
  // TcpConnection��handle*ϵ��, ��set��channel��callback*ϵ��
  // handleRead�л�read, Ȼ����read�ķ���ֵ, ���ݷ���ֵ��������ʲôcb
  // �����ر�����Ҳ��������, ��read����ֵΪ0
  void handleRead(Timestamp receiveTime);
  void handleWrite();
  // ǿ�йر�, ��forceClose����
  // ����Ҫ���ǵ���closeCallback_
  // ���cb��TcpServer::removeConnection
  void handleClose();
  // ���Ǵ�ӡ, ����Ӱ�����ӵ������ر�
  void handleError();
  // void sendInLoop(string&& message);
  void sendInLoop(const StringPiece& message);
  void sendInLoop(const void* message, size_t len);
  void shutdownInLoop();
  // void shutdownAndForceCloseInLoop(double seconds);
  void forceCloseInLoop();
  void setState(StateE s) { state_ = s; }
  const char* stateToString() const;
  void startReadInLoop();
  void stopReadInLoop();

  EventLoop* loop_;
  const string name_;
  StateE state_;  // FIXME: use atomic variable
  bool reading_;
  // we don't expose those classes to client.
  // ����Socket
  // �����������Զ�close fd
  std::unique_ptr<Socket> socket_;
  // ͨ��
  // TcpConnectionʹ��channel�����socket�ϵ�IO�¼�
  std::unique_ptr<Channel> channel_;
  // ��ǰ����˵�ַ
  const InetAddress localAddr_;
  // ��ǰ���ӿͻ��˵�ַ
  const InetAddress peerAddr_;
  // �ص�����
  ConnectionCallback connectionCallback_;
  MessageCallback messageCallback_;
  WriteCompleteCallback writeCompleteCallback_;
  HighWaterMarkCallback highWaterMarkCallback_;
  CloseCallback closeCallback_;
  // ��ˮλ��
  size_t highWaterMark_;
  // ����Buffer
  Buffer inputBuffer_;
  Buffer outputBuffer_; // FIXME: use list<Buffer> as output buffer.

  // ����TcpConnection��context����
  // �������ڱ�����connection�󶨵���������
  // �ȷ�˵connectionid, ������ݵ���ʱ��, userid�ȵ�
  // �����ͻ����ؼ̳�TcpConnection����attach�Լ���״̬
  // ����Ҳ�ò���TcpConnectionFactory��
  boost::any context_;
  // FIXME: creationTime_, lastReceiveTime_
  //        bytesReceived_, bytesSent_
};

typedef std::shared_ptr<TcpConnection> TcpConnectionPtr;

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_TCPCONNECTION_H
