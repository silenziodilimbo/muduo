// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef MUDUO_NET_TCPSERVER_H
#define MUDUO_NET_TCPSERVER_H

#include <muduo/base/Atomic.h>
#include <muduo/base/Types.h>
#include <muduo/net/TcpConnection.h>

#include <map>

namespace muduo
{
namespace net
{

class Acceptor;
class EventLoop;
class EventLoopThreadPool;

///
/// TCP server, supports single-threaded and thread-pool models.
///
/// This is an interface class, so don't expose too much details.
class TcpServer : noncopyable
{
 public:
  typedef std::function<void(EventLoop*)> ThreadInitCallback;
  enum Option
  {
    kNoReusePort,
    kReusePort,
  };

  //TcpServer(EventLoop* loop, const InetAddress& listenAddr);
  TcpServer(EventLoop* loop,
            const InetAddress& listenAddr,
            const string& nameArg,
            Option option = kNoReusePort);
  ~TcpServer();  // force out-line dtor, for std::unique_ptr members.

  const string& ipPort() const { return ipPort_; }
  const string& name() const { return name_; }
  EventLoop* getLoop() const { return loop_; }

  /// Set the number of threads for handling input.
  ///
  /// Always accepts new connection in loop's thread.
  /// Must be called before @c start
  /// @param numThreads
  /// - 0 means all I/O in loop's thread, no thread will created.
  ///   this is the default value.
  /// - 1 means all I/O in another thread.
  /// - N means a thread pool with N threads, new connections
  ///   are assigned on a round-robin basis.
  // �����̳߳����̸߳���
  void setThreadNum(int numThreads);
  // �����̳߳�ʼ���ص�����,һ�����������̷߳���ʱ����ʼ���������һЩ��Դ���ݡ�
  void setThreadInitCallback(const ThreadInitCallback& cb)
  { threadInitCallback_ = cb; }
  /// valid after calling start()
  std::shared_ptr<EventLoopThreadPool> threadPool()
  { return threadPool_; }

  /// Starts the server if it's not listenning.
  ///
  /// It's harmless to call it multiple times.
  /// Thread safe.
  // ��ʼTcpServer����
  void start();

  /// Set connection callback.
  /// Not thread safe.
  // ������·����Or�Ͽ�ʱ�Ļص�
  void setConnectionCallback(const ConnectionCallback& cb)
  { connectionCallback_ = cb; }

  /// Set message callback.
  /// Not thread safe.
  void setMessageCallback(const MessageCallback& cb)
  { messageCallback_ = cb; }

  /// Set write complete callback.
  /// Not thread safe.
  void setWriteCompleteCallback(const WriteCompleteCallback& cb)
  { writeCompleteCallback_ = cb; }

 private:
  /// Not thread safe, but in loop
  // �����ӵ���ʱ���õķ���
  // ��������Ӵ���TcpConnection, ������TcpConnectionPtr����
  void newConnection(int sockfd, const InetAddress& peerAddr);
  /// Thread safe.
  // �Ƴ�һ������
  void removeConnection(const TcpConnectionPtr& conn);
  /// Not thread safe, but in loop
  // �Ƴ�һ�����ӣ���TcpConnection���ڵ�loop
  // ����û�������TcpConnectionPtr�Ļ�, conn�����ü����Ѿ����͵���1
  // ������std::bind��TcpConnection�������ڳ�������connectDestroyed()��ʱ��
  void removeConnectionInLoop(const TcpConnectionPtr& conn);

  // KVӳ��
  typedef std::map<string, TcpConnectionPtr> ConnectionMap;

  EventLoop* loop_;  // the acceptor loop
  const string ipPort_;
  // ����
  const string name_;
  // ���ݽ�����
  // ���Ĺ���
  // Acceptor���ж�accept()�ķ�װ������µ�����
  std::unique_ptr<Acceptor> acceptor_; // avoid revealing Acceptor
  // ����loop���̳߳�
  std::shared_ptr<EventLoopThreadPool> threadPool_;
  ConnectionCallback connectionCallback_;
  MessageCallback messageCallback_;
  WriteCompleteCallback writeCompleteCallback_;
  ThreadInitCallback threadInitCallback_;
  // ��ʼ��־
  AtomicInt32 started_;
  // always in loop thread
  // ��һ������ID
  int nextConnId_;
  // ����TcpConnectionӳ���
  ConnectionMap connections_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_TCPSERVER_H
