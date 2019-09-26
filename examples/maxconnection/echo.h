#ifndef MUDUO_EXAMPLES_MAXCONNECTION_ECHO_H
#define MUDUO_EXAMPLES_MAXCONNECTION_ECHO_H

#include <muduo/net/TcpServer.h>

// RFC 862
class EchoServer
{
 public:
  EchoServer(muduo::net::EventLoop* loop,
             const muduo::net::InetAddress& listenAddr,
             int maxConnections);

  void start();

 private:
  void onConnection(const muduo::net::TcpConnectionPtr& conn);

  void onMessage(const muduo::net::TcpConnectionPtr& conn,
                 muduo::net::Buffer* buf,
                 muduo::Timestamp time);

  muduo::net::TcpServer server_;
	// 当前连接数, 用于在onConnection判断
  int numConnected_; // should be atomic_int, 为了多线程安全
	// 最大连接数
  const int kMaxConnections_;
};

#endif  // MUDUO_EXAMPLES_MAXCONNECTION_ECHO_H
