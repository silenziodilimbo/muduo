#include <muduo/base/Logging.h>
#include <muduo/net/Endian.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpClient.h>

#include <utility>

#include <stdio.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

class TimeClient : noncopyable
{
public:
	TimeClient(EventLoop* loop, const InetAddress& serverAddr)
		: loop_(loop)
		, client_(loop, serverAddr, "TimeClient")
	{
		client_.setConnectionCallback(
			std::bind(&TimeClient::onConnection, this, _1));
		client_.setMessageCallback(
			std::bind(&TimeClient::onMessage, this, _1, _2, _3));
	}

	void connect()
	{
		client_.connect();
	}

private:
	EventLoop* loop_;
	TcpClient client_;

	void onConnection(const TcpConnectionPtr& conn)
	{
		LOG_INFO << conn->localAddress().toIpPort() << " -> "
			<< conn->peerAddress().toIpPort() << " is "
			<< (conn->connected() ? "UP" : "DOWN");
		if (!conn->connected())
		{
			loop_->quit();
		}
	}

	// 发过来的是32-bit整数
	void onMessage(const TcpConnectionPtr& conn,
		Buffer* buf,
		Timestamp receiveTime)
	{
		// 如果数据没有一次性收全
		// 已经收到的数据会累积再Buffer里
		// else分支里并没有调用retrieve*系列函数来取出
		// 以等待后续数据到达,再一起取出
		// 这样即使服务器一个字节一个字节地发送数据,也不会崩
		// 这也是非阻塞网络编程必须在用户态使用接受缓存的主要原因
		if (buf->readableBytes() >= sizeof(int32_t))
		{
			const void* data = buf->peek();
			int32_t be32 = *static_cast<const int32_t*>(data);
			buf->retrieve(sizeof(int32_t));
			time_t time = sockets::networkToHost32(be32);
			Timestamp ts(implicit_cast<uint64_t>(time) * Timestamp::kMicroSecondsPerSecond);
			LOG_INFO << "Server time = " << time << ", " << ts.toFormattedString();
		}
		else
		{
			LOG_INFO << conn->name() << " no enough data " << buf->readableBytes()
				<< " at " << receiveTime.toFormattedString();
		}
	}
};

int main(int argc, char* argv[])
{
	LOG_INFO << "pid = " << getpid();
	if (argc > 1)
	{
		EventLoop loop;
		InetAddress serverAddr(argv[1], 2037);

		TimeClient timeClient(&loop, serverAddr);
		timeClient.connect();
		loop.loop();
	}
	else
	{
		printf("Usage: %s host_ip\n", argv[0]);
	}
}