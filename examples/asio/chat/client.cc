#include "codec.h"

#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
#include <muduo/net/EventLoopThread.h>
#include <muduo/net/TcpClient.h>

#include <iostream>
#include <stdio.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

// 用于处理网络IO
class ChatClient : noncopyable
{
public:
	// 使用了中间层,参考server.cc
	ChatClient(EventLoop& loop, const InetAddress& serverAddr)
		: client_(loop, serverAddr, "ChatClient")
		, codec_(std::bind(&ChatClient::onStringMessage, this, _1, _2, _3))
	{
		client_.setConnectionCallback(
			std::bind(&ChatClient::onConnection, this, _1);
		client_.setMessageCallback(
			std::bind(&ChatClient::onMessage, &codec_, this, _1, _2, _3));
		client_.enableRetry();
	}

	void connect()
	{
		client_.connect();
	}

	void disconnect()
	{
		client_.disconnect();
	}

	void write(const StringPiece& message) // main线程会调用write(),所以要加锁. 这个锁不是为了保护TcpConnection,而是为了保护shared_ptr
	{
		MutexLockGuard lock(mutex_);
		if (connection_)
		{
			codec_.send(get_pointer(connection_), message);
		}
	}
private:
	void onConnection(const TcpConnectionPtr& conn) // onConnection()会有EventLoop线程调用,所以要加锁. 这个锁不是为了保护TcpConnection,而是为了保护shared_ptr
	{
		LOG_INFO << conn->localAddress().toIpPort() << " -> "
			<< conn->peerAddress().toIpPort() << " is "
			<< (conn->connected() ? "UP" : "DOWN");
		MutexLockGuard lock(mutex_);
		if (conn->connected())
		{
			connection_ = conn;
		}
		else
		{
			connection_.reset();
		}
	}

	void onStringMessage(const TcpConnectionPtr&,
		const string& message,
		Timestamp)
	{
		// 这个不用加锁,因为printf是线程安全的
		// 不能用std::cout<<,他不是线程安全的
		printf("<<< %s\n", message.c_str());
	}

	TcpClient client_;
	LengthHeaderCodec codec_;
	MutexLock mutex_;
	TcpConnectionPtr connection_ GUARDED_BY(mutex_);
};

int main(int argc, char* argv[])
{
	LOG_INFO << "pid = " << getpid();
	if (argc > 2)
	{
		// 开辟一个线程.一个loop,用这个loop来启动ChatClient
		// 这是另一个线程,不同于main线程
		EventLoopThread loopThread; 
		uint16_t port = static_cast<uint16_t>(atoi(argv[2]));
		InetAddress serverAddr(argv[1], port);

		ChatClient client(loopThread.startLoop(), serverAddr); // 负责网络IO的client运行于另一个线程loopThread
		client.connect();
		std::string line;
		while (std::getline(std::cin, line))
		{
			client.write(line);
		}
		client.disconnect();
		CurrentThread::sleepUsec(1000 * 1000);  // wait for disconnect, see ace/logging/client.cc
	}
	else
	{
		printf("Usage: %s host_ip port\n", argv[0]);
	}
}