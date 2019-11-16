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

// 它只负责socket的连接, 不负责创建TcpConnection
class Connector : noncopyable,
                  public std::enable_shared_from_this<Connector>
{
 public:
  typedef std::function<void (int sockfd)> NewConnectionCallback;

  Connector(EventLoop* loop, const InetAddress& serverAddr);
  ~Connector();

  // 新连接回调
  void setNewConnectionCallback(const NewConnectionCallback& cb)
  { newConnectionCallback_ = cb; }

  void start();  // can be called in any thread
  void restart();  // must be called in loop thread
  void stop();  // can be called in any thread

  // 服务端地址
  const InetAddress& serverAddress() const { return serverAddr_; }

 private:
   // 连接三种状态  
  enum States { kDisconnected, kConnecting, kConnected };
  // 最大重连时间30s
  static const int kMaxRetryDelayMs = 30*1000;
  // 初始重连时间500ms
  static const int kInitRetryDelayMs = 500;

  // 当前连接状态
  void setState(States s) { state_ = s; }
  // 开始连接
  void startInLoop();
  // 停止连接
  void stopInLoop();
  // 连接
  void connect();
  // 正在连接
  void connecting(int sockfd);
  // 写处理调用
  void handleWrite();
  //出错调用
  void handleError();
  //重试
  void retry(int sockfd);
  //移除和释放Channel
  int removeAndResetChannel();
  //释放Channel
  void resetChannel();

    //IO处理服务
  EventLoop* loop_;
  //服务端地址
  InetAddress serverAddr_;
  //连接状态位
  bool connect_; // atomic
  States state_;  // FIXME: use atomic variable
  //用于连接服务端的Channel
  std::unique_ptr<Channel> channel_;
  //连接成功以后要调用的回调
  NewConnectionCallback newConnectionCallback_;
  //重连间隔
  int retryDelayMs_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_CONNECTOR_H
