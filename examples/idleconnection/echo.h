#ifndef MUDUO_EXAMPLES_IDLECONNECTION_ECHO_H
#define MUDUO_EXAMPLES_IDLECONNECTION_ECHO_H

#include <muduo/net/TcpServer.h>
//#include <muduo/base/Types.h>

#include <unordered_set>

#include <boost/circular_buffer.hpp>

// RFC 862
class EchoServer
{
 public:
  EchoServer(muduo::net::EventLoop* loop,
             const muduo::net::InetAddress& listenAddr,
             int idleSeconds);

  void start();

 private:
  void onConnection(const muduo::net::TcpConnectionPtr& conn);

  void onMessage(const muduo::net::TcpConnectionPtr& conn,
                 muduo::net::Buffer* buf,
                 muduo::Timestamp time);

  void onTimer();

  void dumpConnectionBuckets() const;

  typedef std::weak_ptr<muduo::net::TcpConnection> WeakTcpConnectionPtr;

  // 这就是格子
  // shared_ptr
  // 8个格子, 就是8个桶
  // 析构负责断开链接
  struct Entry : public muduo::copyable
  {
    explicit Entry(const WeakTcpConnectionPtr& weakConn)
      : weakConn_(weakConn)
    {
    }

    ~Entry()
    {
      muduo::net::TcpConnectionPtr conn = weakConn_.lock();
	  // 检查weak_ptr
	  // 如果链接还存在, 断开链接
      if (conn)
      {
        conn->shutdown();
      }
    }

	// 保存Tcp链接的weak_ptr
    WeakTcpConnectionPtr weakConn_;
  };

  // 这个结构本身是个shared_ptr
  typedef std::shared_ptr<Entry> EntryPtr;
  typedef std::weak_ptr<Entry> WeakEntryPtr;
  typedef std::unordered_set<EntryPtr> Bucket;
  typedef boost::circular_buffer<Bucket> WeakConnectionList;

  muduo::net::TcpServer server_;
  WeakConnectionList connectionBuckets_;
};

#endif  // MUDUO_EXAMPLES_IDLECONNECTION_ECHO_H
