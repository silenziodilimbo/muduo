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

  // 获取当前TcpConnection所在的EventLoop
  EventLoop* getLoop() const { return loop_; }
  // TcpConnection名称
  const string& name() const { return name_; }
  // 当前服务端地址
  const InetAddress& localAddress() const { return localAddr_; }
  // 远程连接客户端地址
  const InetAddress& peerAddress() const { return peerAddr_; }
  // 检测是否连接
  bool connected() const { return state_ == kConnected; }
  bool disconnected() const { return state_ == kDisconnected; }
  // return true if success.
  bool getTcpInfo(struct tcp_info*) const;
  string getTcpInfoString() const;

  // void send(string&& message); // C++11 // 三个重载
  // 发送数据
  // 返回值是void,意味者用户不必关心调用send时成功发送了多少字节,muduo库保证发送给对方
  // 非阻塞,用户只要send(),就不会阻塞,即使TCP发送窗口满了
  // 线程安全,源字的,多个线程同时调用send也不会混叠或交织,但是多个线程的先后顺序不确定
  void send(const void* message, int len);
  void send(const StringPiece& message); // StringPiece是Google发明的专门用于传递字符串参数的class,可以用来发送const char*和const std::string&
  // void send(Buffer&& message); // C++11 直接利用std::move 利用右值引用来避免拷贝
  void send(Buffer* message);  // this one will swap data 参数为指针,而不是const引用.因为函数可能使用swap来高效地交换数据,类似右值引用(上面那个)
  // 关闭，里面有一定的处理逻辑
  void shutdown(); // NOT thread safe, no simultaneous calling
  // void shutdownAndForceCloseAfter(double seconds); // NOT thread safe, no simultaneous calling
  // 强行关闭, 最终调用handleClose()
  void forceClose();
  void forceCloseWithDelay(double seconds);
  // 设置tcpNoDelay
  void setTcpNoDelay(bool on);
  // reading or not
  void startRead();
  void stopRead();
  bool isReading() const { return reading_; }; // NOT thread safe, may race with start/stopReadInLoop

  // 设置内容。这个内容可以是任何数据，主要是用着一个临时存储作用。
  void setContext(const boost::any& context)
  { context_ = context; }

  // 获取内容的引用(获取当前内容，一般在回调中使用)
  const boost::any& getContext() const
  { return context_; }

  // 获取内容的地址(获取当前内容，一般在回调中使用)
  boost::any* getMutableContext()
  { return &context_; }

  // 设置连接回调。一般用于做什么?
  // a、连接的建立、连接的销毁、产生关闭事件时会调用此回调，通知外部状态。
  void setConnectionCallback(const ConnectionCallback& cb)
  { connectionCallback_ = cb; }

  // 设置消息回调。一般接收到数据之后会回调此方法
  void setMessageCallback(const MessageCallback& cb)
  { messageCallback_ = cb; }

  // 写完成回调。
  void setWriteCompleteCallback(const WriteCompleteCallback& cb)
  { writeCompleteCallback_ = cb; }

  // 设置高水位回调。
  void setHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t highWaterMark)
  { highWaterMarkCallback_ = cb; highWaterMark_ = highWaterMark; }

  // 获取输入Buffer地址
  /// Advanced interface
  Buffer* inputBuffer()
  { return &inputBuffer_; }

  Buffer* outputBuffer()
  { return &outputBuffer_; }

  // 关闭回调
  /// Internal use only.
  // 这个回调是给TcpServer和TcpClient用的
  // 通知它们移除所持有的TcpConnectionPtr
  // 不是给普通用户用的
  // 普通用户继续使用ConnectionCallback
  void setCloseCallback(const CloseCallback& cb)
  { closeCallback_ = cb; }

  // called when TcpServer accepts a new connection
  void connectEstablished();   // should be called only once
  // called when TcpServer has removed me from its map
  // TcpConnection析构前最后调用的一个函数
  // 它通知用户连接已断开
  void connectDestroyed();  // should be called only once

 private:
  enum StateE { kDisconnected, kConnecting, kConnected, kDisconnecting };
  // TcpConnection的handle*系列, 会set给channel的callback*系列
  // handleRead中会read, 然后检查read的返回值, 根据返回值决定调用什么cb
  // 被动关闭连接也是在这里, 即read返回值为0
  void handleRead(Timestamp receiveTime);
  void handleWrite();
  // 强行关闭, 由forceClose调用
  // 最主要就是调用closeCallback_
  // 这个cb是TcpServer::removeConnection
  void handleClose();
  // 就是打印, 并不影响连接的正常关闭
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
  // 连接Socket
  // 析构函数会自动close fd
  std::unique_ptr<Socket> socket_;
  // 通道
  // TcpConnection使用channel来获得socket上的IO事件
  std::unique_ptr<Channel> channel_;
  // 当前服务端地址
  const InetAddress localAddr_;
  // 当前连接客户端地址
  const InetAddress peerAddr_;
  // 回调函数
  ConnectionCallback connectionCallback_;
  MessageCallback messageCallback_;
  WriteCompleteCallback writeCompleteCallback_;
  HighWaterMarkCallback highWaterMarkCallback_;
  CloseCallback closeCallback_;
  // 高水位线
  size_t highWaterMark_;
  // 输入Buffer
  Buffer inputBuffer_;
  Buffer outputBuffer_; // FIXME: use list<Buffer> as output buffer.

  // 这是TcpConnection的context环境
  // 可以用于保存与connection绑定的任意数据
  // 比方说connectionid, 最后数据到达时间, userid等等
  // 这样客户不必继承TcpConnection就能attach自己的状态
  // 而且也用不着TcpConnectionFactory了
  boost::any context_;
  // FIXME: creationTime_, lastReceiveTime_
  //        bytesReceived_, bytesSent_
};

typedef std::shared_ptr<TcpConnection> TcpConnectionPtr;

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_TCPCONNECTION_H
