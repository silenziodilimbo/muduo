// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/TcpConnection.h>

#include <muduo/base/Logging.h>
#include <muduo/base/WeakCallback.h>
#include <muduo/net/Channel.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/Socket.h>
#include <muduo/net/SocketsOps.h>

#include <errno.h>

using namespace muduo;
using namespace muduo::net;

// 默认连接回调。输出连接状态
void muduo::net::defaultConnectionCallback(const TcpConnectionPtr& conn)
{
  LOG_TRACE << conn->localAddress().toIpPort() << " -> "
            << conn->peerAddress().toIpPort() << " is "
            << (conn->connected() ? "UP" : "DOWN");
  // do not call conn->forceClose(), because some users want to register message callback only.
}

// 默认的有消息时执行的回调。默认取走所有数据
void muduo::net::defaultMessageCallback(const TcpConnectionPtr&,
                                        Buffer* buf,
                                        Timestamp)
{
  buf->retrieveAll();
}

// TcpConnection创建时的值
//Eventloop、名称、套接字、本地服务端地址、客户端地址

//初始化的默认值
//状态、创建一个Socket管理、创建一个通道
//本地地址、远程客户端地址
//高水位标志。FIXME 这个标志目前没理解
TcpConnection::TcpConnection(EventLoop* loop,
                             const string& nameArg,
                             int sockfd,
                             const InetAddress& localAddr,
                             const InetAddress& peerAddr)
  : loop_(CHECK_NOTNULL(loop)),
    // TcpConnection的名称。
    name_(nameArg),
    // 连接状态初始化正在连接中。
    state_(kConnecting),
    reading_(true),
    // 封装sockfd为Socket。
    // TcpConnection没有建立连接得功能, 在构造函数中会传入已经建立好的socket fd, 无论是TcpServer被动还是主动的连接
    socket_(new Socket(sockfd)),
    // 利用loop和sockfd，创建一个通道。
    channel_(new Channel(loop, sockfd)),
    // 本地地址+ 客户端地址。
    localAddr_(localAddr),
    peerAddr_(peerAddr),
    // 设置默认高水位阀值
    highWaterMark_(64*1024*1024)
{
  // 设置读回调，会传一个参数
  channel_->setReadCallback(
      std::bind(&TcpConnection::handleRead, this, _1));
  channel_->setWriteCallback(
      std::bind(&TcpConnection::handleWrite, this));
  channel_->setCloseCallback(
      std::bind(&TcpConnection::handleClose, this));
  channel_->setErrorCallback(
      std::bind(&TcpConnection::handleError, this));
  LOG_DEBUG << "TcpConnection::ctor[" <<  name_ << "] at " << this
            << " fd=" << sockfd;
  // 开启协议栈层心跳
  socket_->setKeepAlive(true);
}

// 析构时就输出下日志，这里没任何资源的释放，
// 应该都是外部进行管理。FIXME 待确认。
TcpConnection::~TcpConnection()
{
  LOG_DEBUG << "TcpConnection::dtor[" <<  name_ << "] at " << this
            << " fd=" << channel_->fd()
            << " state=" << stateToString();
  assert(state_ == kDisconnected);
}

bool TcpConnection::getTcpInfo(struct tcp_info* tcpi) const
{
  return socket_->getTcpInfo(tcpi);
}

string TcpConnection::getTcpInfoString() const
{
  char buf[1024];
  buf[0] = '\0';
  socket_->getTcpInfoString(buf, sizeof buf);
  return buf;
}

void TcpConnection::send(const void* data, int len)
{
  send(StringPiece(static_cast<const char*>(data), len));
}

void TcpConnection::send(const StringPiece& message)
{
  // 处于连接状态才发送
  if (state_ == kConnected)
  {
    // 如果是当前线程就直接发送
    if (loop_->isInLoopThread())
    {
      sendInLoop(message);
    }
    // 如果Loop在别的线程中这放到loop待执行回调队列执行。
    // 会涉及到数据拷贝
    else
    {
      // 把sendInloop加工成函数指针, 放到runInLoop中
      void (TcpConnection::*fp)(const StringPiece& message) = &TcpConnection::sendInLoop;
      loop_->runInLoop(
          std::bind(fp,
                    this,     // FIXME
                    message.as_string()));
                    //std::forward<string>(message)));
    }
  }
}

// 这个send是发送所有数据，要注意效率问题
// FIXME efficiency!!!
void TcpConnection::send(Buffer* buf)
{
  if (state_ == kConnected)
  {
    if (loop_->isInLoopThread())
    {
      sendInLoop(buf->peek(), buf->readableBytes());
      buf->retrieveAll();
    }
    else
    {
      void (TcpConnection::*fp)(const StringPiece& message) = &TcpConnection::sendInLoop;
      loop_->runInLoop(
          std::bind(fp,
                    this,     // FIXME
                    buf->retrieveAllAsString()));
                    //std::forward<string>(message)));
    }
  }
}

void TcpConnection::sendInLoop(const StringPiece& message)
{
  sendInLoop(message.data(), message.size());
}

// 发送数据重点函数
void TcpConnection::sendInLoop(const void* data, size_t len)
{
  loop_->assertInLoopThread();
  ssize_t nwrote = 0;
  size_t remaining = len;
  bool faultError = false;
  if (state_ == kDisconnected)
  {
    LOG_WARN << "disconnected, give up writing";
    return;
  }
  // if no thing in output queue, try writing directly
  if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
  {
    // 如果通道没在写数据，同时输出缓存是空的
    // 则直接往fd中写数据，即发送
    // 如果当前outputBuffer_有待发送的数据, 那么就不能先尝试发送了, 这会造成数据乱序
    nwrote = sockets::write(channel_->fd(), data, len);
    if (nwrote >= 0)
    {
      // 发送数据 >= 0
      remaining = len - nwrote;
      if (remaining == 0 && writeCompleteCallback_)
      {
        // 若数据一次性都发完了，同时也设置了写完成回调。
        // 则调用下写完成回调函数。
        loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
      }
    }
    else // nwrote < 0
    {
      // 如果写返回值小于0，表示写出错
      // 则把已写数据大小设置为0。
      nwrote = 0;
      if (errno != EWOULDBLOCK)
      {
        LOG_SYSERR << "TcpConnection::sendInLoop";
        if (errno == EPIPE || errno == ECONNRESET) // FIXME: any others?
        {
          faultError = true;
        }
      }
    }
  }

  // a、不直接发送数据情况。
  // b、数据未发送完的情况。
  assert(remaining <= len);
  if (!faultError && remaining > 0)
  {
    size_t oldLen = outputBuffer_.readableBytes();
    if (oldLen + remaining >= highWaterMark_
        && oldLen < highWaterMark_
        && highWaterMarkCallback_)
    {
      // 添加新的待发送数据之后，如果数据大小已超过设置的警戒线
      // 则回调下设置的高水平阀值回调函数，对现有的长度做出处理。
      // 高水平水位线的使用场景?
      loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
    }
    // 往outputBuffer后面添加数据。涉及到数据的拷贝
    outputBuffer_.append(static_cast<const char*>(data)+nwrote, remaining);
    if (!channel_->isWriting())
    {
      // 将通道置成可写状态。这样当通道活跃时，
      // 就好调用TcpConnection的可写方法。
      // 对实时要求高的数据，这种处理方法可能有一定的延时。
      channel_->enableWriting();
    }
  }
}

// 关闭"写"方向的连接,而不关闭"读"方面的连接
// 关闭动作，如果状态是连接，
// 则要调用下关闭动作。
void TcpConnection::shutdown()
{
  // FIXME: use compare and swap
  if (state_ == kConnected)
  {
    setState(kDisconnecting);
    // FIXME: shared_from_this()?
	// 调用TcpConnection::shutdownInLoop()
    loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
  }
}

// a、通道不再写数据，则直接关闭写
// b、通道若处于写数据状态，则不做
//   处理，留给后面处理。
//   后面是指在handleWrite的时候，
//   如果发现状态是断开，则调用shutdownWrite
void TcpConnection::shutdownInLoop()
{
  loop_->assertInLoopThread();
  if (!channel_->isWriting()) // 如果已经写完了
  {
    // we are not writing
    socket_->shutdownWrite(); // 关闭"写"的连接
  }
}

// void TcpConnection::shutdownAndForceCloseAfter(double seconds)
// {
//   // FIXME: use compare and swap
//   if (state_ == kConnected)
//   {
//     setState(kDisconnecting);
//     loop_->runInLoop(std::bind(&TcpConnection::shutdownAndForceCloseInLoop, this, seconds));
//   }
// }

// void TcpConnection::shutdownAndForceCloseInLoop(double seconds)
// {
//   loop_->assertInLoopThread();
//   if (!channel_->isWriting())
//   {
//     // we are not writing
//     socket_->shutdownWrite();
//   }
//   loop_->runAfter(
//       seconds,
//       makeWeakCallback(shared_from_this(),
//                        &TcpConnection::forceCloseInLoop));
// }

void TcpConnection::forceClose()
{
  // FIXME: use compare and swap
  if (state_ == kConnected || state_ == kDisconnecting)
  {
    setState(kDisconnecting);
    loop_->queueInLoop(std::bind(&TcpConnection::forceCloseInLoop, shared_from_this()));
  }
}

void TcpConnection::forceCloseWithDelay(double seconds)
{
  if (state_ == kConnected || state_ == kDisconnecting)
  {
    setState(kDisconnecting);
    loop_->runAfter(
        seconds,
        makeWeakCallback(shared_from_this(),
                         &TcpConnection::forceClose));  // not forceCloseInLoop to avoid race condition
  }
}

void TcpConnection::forceCloseInLoop()
{
  loop_->assertInLoopThread();
  if (state_ == kConnected || state_ == kDisconnecting)
  {
    // as if we received 0 byte in handleRead();
    handleClose();
  }
}

const char* TcpConnection::stateToString() const
{
  switch (state_)
  {
    case kDisconnected:
      return "kDisconnected";
    case kConnecting:
      return "kConnecting";
    case kConnected:
      return "kConnected";
    case kDisconnecting:
      return "kDisconnecting";
    default:
      return "unknown state";
  }
}

// 开启TcpNoDelay状态，禁用Nagle算法。
// 开启目的是避免连续发包出现延迟，
// 这对编写低延迟网络服务很重要
void TcpConnection::setTcpNoDelay(bool on)
{
  socket_->setTcpNoDelay(on);
}

void TcpConnection::startRead()
{
  loop_->runInLoop(std::bind(&TcpConnection::startReadInLoop, this));
}

void TcpConnection::startReadInLoop()
{
  loop_->assertInLoopThread();
  if (!reading_ || !channel_->isReading())
  {
    channel_->enableReading();
    reading_ = true;
  }
}

void TcpConnection::stopRead()
{
  loop_->runInLoop(std::bind(&TcpConnection::stopReadInLoop, this));
}

void TcpConnection::stopReadInLoop()
{
  loop_->assertInLoopThread();
  if (reading_ || channel_->isReading())
  {
    channel_->disableReading();
    reading_ = false;
  }
}

// 连接建立完成方法，
// 当TcpServer accepts a new connection时，调用此方法
// a、设置下状态
// b、通道tie下，并设置可读
// c、调用下连接建立完成的回调函数
void TcpConnection::connectEstablished()
{
  loop_->assertInLoopThread();
  assert(state_ == kConnecting);
  setState(kConnected);
  channel_->tie(shared_from_this());
  channel_->enableReading();

  // 输出一些信息
  connectionCallback_(shared_from_this());
}

// 连接销毁。当TcpServer将TcpConnection从
// map列表中清除时，会调用此方法。
// a、设置下状态
// b、关闭通道
// c、调用下连接回调函数。 通知用户连接已断开
// d、移除通道。
// TcpConnection析构前最后调用的一个函数
void TcpConnection::connectDestroyed()
{
  loop_->assertInLoopThread();
  if (state_ == kConnected)
  {
    setState(kDisconnected);
    channel_->disableAll();

    connectionCallback_(shared_from_this());
  }
  // 移除当前通道
  channel_->remove();
}

// 有可读事件时.
void TcpConnection::handleRead(Timestamp receiveTime)
{
  loop_->assertInLoopThread();
  int savedErrno = 0;
  // 直接将数据读到inputBuffer
  ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
  // 然后检查read的返回值, 根据返回值决定调用什么cb
  // 被动关闭连接也是在这里
  if (n > 0)
  {
    // a、读取数据大于0，调用下回调
    // messageCallback_ 是用户set的, 也就是用户在main函数中, server.setMessageCallback
    messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
  }
  else if (n == 0)
  {
    // b、等于0表示要socket关闭
    handleClose();
  }
  else
  {
    // c、小于0表示有错误。 
    errno = savedErrno;
    LOG_SYSERR << "TcpConnection::handleRead";
    handleError();
  }
}

// 回调调用可写函数
// 该函数用于强行关闭连接
// 一般而言,当对方read()到0字节之后,会主动关闭连接(无论是shutdownWrite()还是close())
// 但如果对方故意不关闭,muduo的连接就会一直半开着,消耗系统资源
// 所以必要时可以调用handleWrite来强行关闭连接
void TcpConnection::handleWrite()
{
  loop_->assertInLoopThread();
  // 通道可写才进入
  if (channel_->isWriting())
  {
    // 写缓存里所有数据   
    size_t n = sockets::write(channel_->fd(),
                               outputBuffer_.peek(),
                               outputBuffer_.readableBytes());
    if (n > 0)
    {
      // 发送了多少数据，设置Buffer索引，
      // 当外部调用TcpConnection::shutdown时也不直接关闭
      // 要等数据发送完了之后再关闭。
      outputBuffer_.retrieve(n);
      if (outputBuffer_.readableBytes() == 0)
      {
        // 如果Buffer可读数据为0表示都已经发送完毕。
        // 关闭通道的写状态。
        channel_->disableWriting();
        if (writeCompleteCallback_)
        {
          // 如果有写完成回调函数，就调用下。
          loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
        }
        // 如果状态已经是断开中，
        // 则要关闭。FIXME_hqb 是哪里设置的呢?
        // TcpConnection::shutdown调用时会设置状态为kDisconnecting
        if (state_ == kDisconnecting)
        {
          shutdownInLoop();
        }
      }
    }
    else
    {
      LOG_SYSERR << "TcpConnection::handleWrite";
      // if (state_ == kDisconnecting)
      // {
      //   shutdownInLoop();
      // }
    }
  }
  else
  {
    LOG_TRACE << "Connection fd = " << channel_->fd()
              << " is down, no more writing";
  }
}

// 连接关闭
// 强行关闭, 由forceClose调用
// 最主要就是调用closeCallback_
// 这个cb是TcpServer::removeConnection
// 这里fd不关闭，fd是外部传入的
// 当TcpConnection析构时，Sockets会析构
// 由Sockets去关闭socket
void TcpConnection::handleClose()
{
  loop_->assertInLoopThread();
  LOG_TRACE << "fd = " << channel_->fd() << " state = " << stateToString();
  assert(state_ == kConnected || state_ == kDisconnecting);
  // we don't close fd, leave it to dtor, so we can find leaks easily.
  setState(kDisconnected);
  channel_->disableAll();

  TcpConnectionPtr guardThis(shared_from_this());
  connectionCallback_(guardThis);
  // must be the last line
  // 最主要就是调用closeCallback_
  // 这个cb是TcpServer::removeConnection
  closeCallback_(guardThis);
}

// 输出下错误日志。
void TcpConnection::handleError()
{
  int err = sockets::getSocketError(channel_->fd());
  LOG_ERROR << "TcpConnection::handleError [" << name_
            << "] - SO_ERROR = " << err << " " << strerror_tl(err);
}

