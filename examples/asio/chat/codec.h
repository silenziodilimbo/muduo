#ifndef MUDUO_EXAMPLES_ASIO_CHAT_CODEC_H
#define MUDUO_EXAMPLES_ASIO_CHAT_CODEC_H

#include <muduo/base/Logging.h>
#include <muduo/net/Buffer.h>
#include <muduo/net/Endian.h>
#include <muduo/net/TcpConnection.h>

class LengthHeaderCodec : muduo::noncopyable
{
public:
	typedef std::function<void(const muduo::net::TcpConnectionPtr&,
		const muduo::string& message,
		muduo::Timestamp)> StringMessageCallback;

// 构造,参数为一个cb
	explicit LengthHeaderCodec(const StringMessageCallback& cb)
		:messageCallback_(cb)
	{
	}

	// onMessage的第二参数buf
	// 使用上面的函数类型做了一个转换
	// 调用回调(onStringMessage)的时候,第二参数是const string&
	// 使得用户代码不必关心分包操作,得到的是完整的body
	void onMessage(const muduo::net::TcpConnectionPtr& conn,
		muduo::net::Buffer* buf,
		muduo::Timestamp receiveTime)
	{
		while (buf->readableBytes() >= kHeaderLen) // 反复读取数据,直到buffer中的数据不够一条完整的消息,kHeaderLen == 4 // sizeof(int32_t)
		{
			// 完整长度:发送方添加的消息长度
			// 完整body:发送方发送的body
			// 当前body:接受方目前收到的(在这个循环内的)消息,可能不完整,因为后面的还没发过来呢
			// 当前长度:对当前消息判断长度得到的数字,如果没接收完,将小于完整长度

			// buf是完整长度+当前消息
			// 而data是当前body buf->peek();
			const void* data = buf->peek();
			int32_t be32 = *static_cast<const int32_t*>(data); // 把void*转换成int32_t
																												 // Linux平台上执行malloc()，如果没有足够的RAM，Linux不是让malloc()失败返回， 
																 // 而是向当前进程分发SIGBUS信号。
																 // SIGBUS的缺省行为是终止当前进程并产 生core dump。
			// len是当前body的长度
			const int32_t len = muduo::net::sockets::networkToHost32(be32);
			if (len > 65535 || len < 0)
			{
				LOG_ERROR << "Invalid length " << len;
				conn->shutdown();
				break;
			}
			// buf->readableBytes()是由发送方添加的,消息的完整长度
			else if (buf->readableBytes() >= len + kHeaderLen) // 判断完整长度是否等于本循环内的当前消息长度+4
			{
				// 如果进来了,就是本条消息已经完整接受,可以处理了
				// 如果不完整,就在下面的else中继续循环
				buf->retrieve(kHeaderLen);// 先从buf里取出kHeaderLen个字节,即完整长度
				muduo::string message(buf->peek(), len); // 构造完整消息,由当前(等同于完整)body,当前(等同于完整)消息长度组成
				messageCallback_(conn, message, receiveTime); // 调用回调,这个回调是参数
				buf->retrieve(len); // 从buf里取出len个字节,即body
														// 并没有全部取出
														// 如果有两条消息一起到达,将很快进入第二次while循环,取第二条消息
			}
			else
			{
				break;
			}
		}
	}

	void send(const muduo::net::TcpConnectionPtr& conn,
		const muduo::StringPiece& message)
	{
		muduo::net::Buffer buf;
		buf.append(message.data(), message.size()); // 存入body
		int32_t len = static_cast<int32_t>(message.size());
		int32_t be32 = muduo::net::sockets::hostToNetwork32(len); // 判断body长度
		buf.prepend(&be32, sizeof be32); // 把body长度添加到完整body的前面,形成完整消息==完整长度+完整body
		conn->send(&buf);
	}
private:
	StringMessageCallback messageCallback_;
	const static size_t kHeaderLen = sizeof(int32_t);
};

#endif  //MUDUO_EXAMPLES_ASIO_CHAT_CODEC_H
