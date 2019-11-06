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
  // 设置线程池中线程个数
  void setThreadNum(int numThreads);
  // 设置线程初始化回调函数,一般用于启动线程服务时，初始化调用这得一些资源数据。
  void setThreadInitCallback(const ThreadInitCallback& cb)
  { threadInitCallback_ = cb; }
  /// valid after calling start()
  std::shared_ptr<EventLoopThreadPool> threadPool()
  { return threadPool_; }

  /// Starts the server if it's not listenning.
  ///
  /// It's harmless to call it multiple times.
  /// Thread safe.
  // 开始TcpServer服务
  void start();

  /// Set connection callback.
  /// Not thread safe.
  // 设置链路连接Or断开时的回调
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
  // 新连接到达时调用的方法
  // 会给新连接创建TcpConnection, 保存在TcpConnectionPtr里面
  void newConnection(int sockfd, const InetAddress& peerAddr);
  /// Thread safe.
  // 移除一个连接
  void removeConnection(const TcpConnectionPtr& conn);
  /// Not thread safe, but in loop
  // 移除一个连接，用TcpConnection所在的loop
  // 如果用户不持有TcpConnectionPtr的话, conn的引用计数已经降低到了1
  // 这里用std::bind让TcpConnection的生命期长到调用connectDestroyed()的时刻
  void removeConnectionInLoop(const TcpConnectionPtr& conn);

  // KV映射
  typedef std::map<string, TcpConnectionPtr> ConnectionMap;

  EventLoop* loop_;  // the acceptor loop
  const string ipPort_;
  // 名称
  const string name_;
  // 数据接收器
  // 核心功能
  // Acceptor中有对accept()的封装，获得新的连接
  std::unique_ptr<Acceptor> acceptor_; // avoid revealing Acceptor
  // 用于loop的线程池
  std::shared_ptr<EventLoopThreadPool> threadPool_;
  ConnectionCallback connectionCallback_;
  MessageCallback messageCallback_;
  WriteCompleteCallback writeCompleteCallback_;
  ThreadInitCallback threadInitCallback_;
  // 开始标志
  AtomicInt32 started_;
  // always in loop thread
  // 下一个连接ID
  int nextConnId_;
  // 存活的TcpConnection映射表
  ConnectionMap connections_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_TCPSERVER_H
