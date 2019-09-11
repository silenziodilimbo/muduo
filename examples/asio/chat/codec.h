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

// ����,����Ϊһ��cb
	explicit LengthHeaderCodec(const StringMessageCallback& cb)
		:messageCallback_(cb)
	{
	}

	// onMessage�ĵڶ�����buf
	// ʹ������ĺ�����������һ��ת��
	// ���ûص�(onStringMessage)��ʱ��,�ڶ�������const string&
	// ʹ���û����벻�ع��ķְ�����,�õ�����������body
	void onMessage(const muduo::net::TcpConnectionPtr& conn,
		muduo::net::Buffer* buf,
		muduo::Timestamp receiveTime)
	{
		while (buf->readableBytes() >= kHeaderLen) // ������ȡ����,ֱ��buffer�е����ݲ���һ����������Ϣ,kHeaderLen == 4 // sizeof(int32_t)
		{
			// ��������:���ͷ���ӵ���Ϣ����
			// ����body:���ͷ����͵�body
			// ��ǰbody:���ܷ�Ŀǰ�յ���(�����ѭ���ڵ�)��Ϣ,���ܲ�����,��Ϊ����Ļ�û��������
			// ��ǰ����:�Ե�ǰ��Ϣ�жϳ��ȵõ�������,���û������,��С����������

			// buf����������+��ǰ��Ϣ
			// ��data�ǵ�ǰbody buf->peek();
			const void* data = buf->peek();
			int32_t be32 = *static_cast<const int32_t*>(data); // ��void*ת����int32_t
																												 // Linuxƽ̨��ִ��malloc()�����û���㹻��RAM��Linux������malloc()ʧ�ܷ��أ� 
																 // ������ǰ���̷ַ�SIGBUS�źš�
																 // SIGBUS��ȱʡ��Ϊ����ֹ��ǰ���̲��� ��core dump��
			// len�ǵ�ǰbody�ĳ���
			const int32_t len = muduo::net::sockets::networkToHost32(be32);
			if (len > 65535 || len < 0)
			{
				LOG_ERROR << "Invalid length " << len;
				conn->shutdown();
				break;
			}
			// buf->readableBytes()���ɷ��ͷ���ӵ�,��Ϣ����������
			else if (buf->readableBytes() >= len + kHeaderLen) // �ж����������Ƿ���ڱ�ѭ���ڵĵ�ǰ��Ϣ����+4
			{
				// ���������,���Ǳ�����Ϣ�Ѿ���������,���Դ�����
				// ���������,���������else�м���ѭ��
				buf->retrieve(kHeaderLen);// �ȴ�buf��ȡ��kHeaderLen���ֽ�,����������
				muduo::string message(buf->peek(), len); // ����������Ϣ,�ɵ�ǰ(��ͬ������)body,��ǰ(��ͬ������)��Ϣ�������
				messageCallback_(conn, message, receiveTime); // ���ûص�,����ص��ǲ���
				buf->retrieve(len); // ��buf��ȡ��len���ֽ�,��body
														// ��û��ȫ��ȡ��
														// �����������Ϣһ�𵽴�,���ܿ����ڶ���whileѭ��,ȡ�ڶ�����Ϣ
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
		buf.append(message.data(), message.size()); // ����body
		int32_t len = static_cast<int32_t>(message.size());
		int32_t be32 = muduo::net::sockets::hostToNetwork32(len); // �ж�body����
		buf.prepend(&be32, sizeof be32); // ��body������ӵ�����body��ǰ��,�γ�������Ϣ==��������+����body
		conn->send(&buf);
	}
private:
	StringMessageCallback messageCallback_;
	const static size_t kHeaderLen = sizeof(int32_t);
};

#endif  //MUDUO_EXAMPLES_ASIO_CHAT_CODEC_H
