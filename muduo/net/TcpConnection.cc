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

// Ĭ�����ӻص����������״̬
void muduo::net::defaultConnectionCallback(const TcpConnectionPtr& conn)
{
  LOG_TRACE << conn->localAddress().toIpPort() << " -> "
            << conn->peerAddress().toIpPort() << " is "
            << (conn->connected() ? "UP" : "DOWN");
  // do not call conn->forceClose(), because some users want to register message callback only.
}

// Ĭ�ϵ�����Ϣʱִ�еĻص���Ĭ��ȡ����������
void muduo::net::defaultMessageCallback(const TcpConnectionPtr&,
                                        Buffer* buf,
                                        Timestamp)
{
  buf->retrieveAll();
}

// TcpConnection����ʱ��ֵ
//Eventloop�����ơ��׽��֡����ط���˵�ַ���ͻ��˵�ַ

//��ʼ����Ĭ��ֵ
//״̬������һ��Socket��������һ��ͨ��
//���ص�ַ��Զ�̿ͻ��˵�ַ
//��ˮλ��־��FIXME �����־Ŀǰû���
TcpConnection::TcpConnection(EventLoop* loop,
                             const string& nameArg,
                             int sockfd,
                             const InetAddress& localAddr,
                             const InetAddress& peerAddr)
  : loop_(CHECK_NOTNULL(loop)),
    // TcpConnection�����ơ�
    name_(nameArg),
    // ����״̬��ʼ�����������С�
    state_(kConnecting),
    reading_(true),
    // ��װsockfdΪSocket��
    // TcpConnectionû�н������ӵù���, �ڹ��캯���лᴫ���Ѿ������õ�socket fd, ������TcpServer������������������
    socket_(new Socket(sockfd)),
    // ����loop��sockfd������һ��ͨ����
    channel_(new Channel(loop, sockfd)),
    // ���ص�ַ+ �ͻ��˵�ַ��
    localAddr_(localAddr),
    peerAddr_(peerAddr),
    // ����Ĭ�ϸ�ˮλ��ֵ
    highWaterMark_(64*1024*1024)
{
  // ���ö��ص����ᴫһ������
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
  // ����Э��ջ������
  socket_->setKeepAlive(true);
}

// ����ʱ���������־������û�κ���Դ���ͷţ�
// Ӧ�ö����ⲿ���й���FIXME ��ȷ�ϡ�
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
  // ��������״̬�ŷ���
  if (state_ == kConnected)
  {
    // ����ǵ�ǰ�߳̾�ֱ�ӷ���
    if (loop_->isInLoopThread())
    {
      sendInLoop(message);
    }
    // ���Loop�ڱ���߳�����ŵ�loop��ִ�лص�����ִ�С�
    // ���漰�����ݿ���
    else
    {
      // ��sendInloop�ӹ��ɺ���ָ��, �ŵ�runInLoop��
      void (TcpConnection::*fp)(const StringPiece& message) = &TcpConnection::sendInLoop;
      loop_->runInLoop(
          std::bind(fp,
                    this,     // FIXME
                    message.as_string()));
                    //std::forward<string>(message)));
    }
  }
}

// ���send�Ƿ����������ݣ�Ҫע��Ч������
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

// ���������ص㺯��
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
    // ���ͨ��û��д���ݣ�ͬʱ��������ǿյ�
    // ��ֱ����fd��д���ݣ�������
    // �����ǰoutputBuffer_�д����͵�����, ��ô�Ͳ����ȳ��Է�����, ��������������
    nwrote = sockets::write(channel_->fd(), data, len);
    if (nwrote >= 0)
    {
      // �������� >= 0
      remaining = len - nwrote;
      if (remaining == 0 && writeCompleteCallback_)
      {
        // ������һ���Զ������ˣ�ͬʱҲ������д��ɻص���
        // �������д��ɻص�������
        loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
      }
    }
    else // nwrote < 0
    {
      // ���д����ֵС��0����ʾд����
      // �����д���ݴ�С����Ϊ0��
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

  // a����ֱ�ӷ������������
  // b������δ������������
  assert(remaining <= len);
  if (!faultError && remaining > 0)
  {
    size_t oldLen = outputBuffer_.readableBytes();
    if (oldLen + remaining >= highWaterMark_
        && oldLen < highWaterMark_
        && highWaterMarkCallback_)
    {
      // ����µĴ���������֮��������ݴ�С�ѳ������õľ�����
      // ��ص������õĸ�ˮƽ��ֵ�ص������������еĳ�����������
      // ��ˮƽˮλ�ߵ�ʹ�ó���?
      loop_->queueInLoop(std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
    }
    // ��outputBuffer����������ݡ��漰�����ݵĿ���
    outputBuffer_.append(static_cast<const char*>(data)+nwrote, remaining);
    if (!channel_->isWriting())
    {
      // ��ͨ���óɿ�д״̬��������ͨ����Ծʱ��
      // �ͺõ���TcpConnection�Ŀ�д������
      // ��ʵʱҪ��ߵ����ݣ����ִ�����������һ������ʱ��
      channel_->enableWriting();
    }
  }
}

// �ر�"д"���������,�����ر�"��"���������
// �رն��������״̬�����ӣ�
// ��Ҫ�����¹رն�����
void TcpConnection::shutdown()
{
  // FIXME: use compare and swap
  if (state_ == kConnected)
  {
    setState(kDisconnecting);
    // FIXME: shared_from_this()?
	// ����TcpConnection::shutdownInLoop()
    loop_->runInLoop(std::bind(&TcpConnection::shutdownInLoop, this));
  }
}

// a��ͨ������д���ݣ���ֱ�ӹر�д
// b��ͨ��������д����״̬������
//   �����������洦��
//   ������ָ��handleWrite��ʱ��
//   �������״̬�ǶϿ��������shutdownWrite
void TcpConnection::shutdownInLoop()
{
  loop_->assertInLoopThread();
  if (!channel_->isWriting()) // ����Ѿ�д����
  {
    // we are not writing
    socket_->shutdownWrite(); // �ر�"д"������
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

// ����TcpNoDelay״̬������Nagle�㷨��
// ����Ŀ���Ǳ����������������ӳ٣�
// ��Ա�д���ӳ�����������Ҫ
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

// ���ӽ�����ɷ�����
// ��TcpServer accepts a new connectionʱ�����ô˷���
// a��������״̬
// b��ͨ��tie�£������ÿɶ�
// c�����������ӽ�����ɵĻص�����
void TcpConnection::connectEstablished()
{
  loop_->assertInLoopThread();
  assert(state_ == kConnecting);
  setState(kConnected);
  channel_->tie(shared_from_this());
  channel_->enableReading();

  // ���һЩ��Ϣ
  connectionCallback_(shared_from_this());
}

// �������١���TcpServer��TcpConnection��
// map�б������ʱ������ô˷�����
// a��������״̬
// b���ر�ͨ��
// c�����������ӻص������� ֪ͨ�û������ѶϿ�
// d���Ƴ�ͨ����
// TcpConnection����ǰ�����õ�һ������
void TcpConnection::connectDestroyed()
{
  loop_->assertInLoopThread();
  if (state_ == kConnected)
  {
    setState(kDisconnected);
    channel_->disableAll();

    connectionCallback_(shared_from_this());
  }
  // �Ƴ���ǰͨ��
  channel_->remove();
}

// �пɶ��¼�ʱ.
void TcpConnection::handleRead(Timestamp receiveTime)
{
  loop_->assertInLoopThread();
  int savedErrno = 0;
  // ֱ�ӽ����ݶ���inputBuffer
  ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
  // Ȼ����read�ķ���ֵ, ���ݷ���ֵ��������ʲôcb
  // �����ر�����Ҳ��������
  if (n > 0)
  {
    // a����ȡ���ݴ���0�������»ص�
    // messageCallback_ ���û�set��, Ҳ�����û���main������, server.setMessageCallback
    messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
  }
  else if (n == 0)
  {
    // b������0��ʾҪsocket�ر�
    handleClose();
  }
  else
  {
    // c��С��0��ʾ�д��� 
    errno = savedErrno;
    LOG_SYSERR << "TcpConnection::handleRead";
    handleError();
  }
}

// �ص����ÿ�д����
// �ú�������ǿ�йر�����
// һ�����,���Է�read()��0�ֽ�֮��,�������ر�����(������shutdownWrite()����close())
// ������Է����ⲻ�ر�,muduo�����Ӿͻ�һֱ�뿪��,����ϵͳ��Դ
// ���Ա�Ҫʱ���Ե���handleWrite��ǿ�йر�����
void TcpConnection::handleWrite()
{
  loop_->assertInLoopThread();
  // ͨ����д�Ž���
  if (channel_->isWriting())
  {
    // д��������������   
    size_t n = sockets::write(channel_->fd(),
                               outputBuffer_.peek(),
                               outputBuffer_.readableBytes());
    if (n > 0)
    {
      // �����˶������ݣ�����Buffer������
      // ���ⲿ����TcpConnection::shutdownʱҲ��ֱ�ӹر�
      // Ҫ�����ݷ�������֮���ٹرա�
      outputBuffer_.retrieve(n);
      if (outputBuffer_.readableBytes() == 0)
      {
        // ���Buffer�ɶ�����Ϊ0��ʾ���Ѿ�������ϡ�
        // �ر�ͨ����д״̬��
        channel_->disableWriting();
        if (writeCompleteCallback_)
        {
          // �����д��ɻص��������͵����¡�
          loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
        }
        // ���״̬�Ѿ��ǶϿ��У�
        // ��Ҫ�رա�FIXME_hqb ���������õ���?
        // TcpConnection::shutdown����ʱ������״̬ΪkDisconnecting
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

// ���ӹر�
// ǿ�йر�, ��forceClose����
// ����Ҫ���ǵ���closeCallback_
// ���cb��TcpServer::removeConnection
// ����fd���رգ�fd���ⲿ�����
// ��TcpConnection����ʱ��Sockets������
// ��Socketsȥ�ر�socket
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
  // ����Ҫ���ǵ���closeCallback_
  // ���cb��TcpServer::removeConnection
  closeCallback_(guardThis);
}

// ����´�����־��
void TcpConnection::handleError()
{
  int err = sockets::getSocketError(channel_->fd());
  LOG_ERROR << "TcpConnection::handleError [" << name_
            << "] - SO_ERROR = " << err << " " << strerror_tl(err);
}

