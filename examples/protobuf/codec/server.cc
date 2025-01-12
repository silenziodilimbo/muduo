#include "codec.h"
#include "dispatcher.h"
#include <examples/protobuf/codec/query.pb.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Mutex.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>

#include <stdio.h>
#include <unistd.h>

using namespace muduo;
using namespace muduo::net;

typedef std::shared_ptr<muduo::Query> QueryPtr;
typedef std::shared_ptr<muduo::Answer> AnswerPtr;

class QueryServer : noncopyable
{
 public:
  QueryServer(EventLoop* loop,
              const InetAddress& listenAddr)
  : server_(loop, listenAddr, "QueryServer"),
		// onUnknownMessage dispatcher_收到消息, 分发消息
    dispatcher_(std::bind(&QueryServer::onUnknownMessage, this, _1, _2, _3)),
		// onProtobufMessage codec_解码之后, 触发Dispatcher的回调
    codec_(std::bind(&ProtobufDispatcher::onProtobufMessage, &dispatcher_, _1, _2, _3))
  {
		// dispatcher_ 注册具体的消息回调, Dispatcher分发消息后调用
    dispatcher_.registerMessageCallback<muduo::Query>(
        std::bind(&QueryServer::onQuery, this, _1, _2, _3));
		// dispatcher_ 注册具体的消息回调, Dispatcher分发消息后调用
    dispatcher_.registerMessageCallback<muduo::Answer>(
        std::bind(&QueryServer::onAnswer, this, _1, _2, _3));
		// 业务
		// onConnection server_收到消息, 触发QueryServer的回调
    server_.setConnectionCallback(
        std::bind(&QueryServer::onConnection, this, _1));
		// onMessage server_收到消息, 献给codec解码
    server_.setMessageCallback(
        std::bind(&ProtobufCodec::onMessage, &codec_, _1, _2, _3));
  }

  void start()
  {
    server_.start();
  }

 private:
	 // 建立连接
  void onConnection(const TcpConnectionPtr& conn)
  {
    LOG_INFO << conn->localAddress().toIpPort() << " -> "
        << conn->peerAddress().toIpPort() << " is "
        << (conn->connected() ? "UP" : "DOWN");
  }

	// 由dispatcher_调用, 分发消息后调用这个回调
	// 直接断开连接
  void onUnknownMessage(const TcpConnectionPtr& conn,
                        const MessagePtr& message,
                        Timestamp)
  {
    LOG_INFO << "onUnknownMessage: " << message->GetTypeName();
    conn->shutdown();
  }

	// 由dispatcher_调用, 分发消息后调用这个回调
	// 就是回一个消息
  void onQuery(const muduo::net::TcpConnectionPtr& conn,
               const QueryPtr& message,
               muduo::Timestamp)
  {
    LOG_INFO << "onQuery:\n" << message->GetTypeName() << message->DebugString();
    Answer answer;
    answer.set_id(1);
    answer.set_questioner("Chen Shuo");
    answer.set_answerer("blog.csdn.net/Solstice");
    answer.add_solution("Jump!");
    answer.add_solution("Win!");
    codec_.send(conn, answer);

    conn->shutdown();
  }

	// 由dispatcher_调用, 分发消息后调用这个回调
  void onAnswer(const muduo::net::TcpConnectionPtr& conn,
                const AnswerPtr& message,
                muduo::Timestamp)
  {
    LOG_INFO << "onAnswer: " << message->GetTypeName();
    conn->shutdown();
  }

  TcpServer server_;
  ProtobufDispatcher dispatcher_;
  ProtobufCodec codec_;
};

int main(int argc, char* argv[])
{
  LOG_INFO << "pid = " << getpid();
  if (argc > 1)
  {
    EventLoop loop;
    uint16_t port = static_cast<uint16_t>(atoi(argv[1]));
    InetAddress serverAddr(port);
    QueryServer server(&loop, serverAddr);
    server.start();
    loop.loop();
  }
  else
  {
    printf("Usage: %s port\n", argv[0]);
  }
}

