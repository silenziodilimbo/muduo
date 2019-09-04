#include "echo.h"

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>

#include <unistd.h>

int main()
{
	LOG_INFO << "pid =" << getpid();
	muduo::net::EventLoop loop; // 创建loop
	muduo::net::InetAddress listenAddr(2007); // 创建端口
	EchoServer server(&loop, listenAddr); // 创建业务对象
										  // 第一参数为指向loop的指针
										  // 第二参数为listenAddr的引用传递
										  // 这两个参数,以及对象名字"EchoServer"将传入server_
										  // 并调用server_.setConnectionCallback和server_.setMessageCallback两个方法
										  // 将自己的回调进行绑定
	server.start(); // 内部是server_.start()
	loop.loop(); // 事件驱动编程,
				 // 在注册回调,并start()之后不需要主动做什么,而是被动等待事件发生
}