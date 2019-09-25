// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//

#include <muduo/net/Buffer.h>

#include <muduo/net/SocketsOps.h>

#include <errno.h>
#include <sys/uio.h>

using namespace muduo;
using namespace muduo::net;

const char Buffer::kCRLF[] = "\r\n";

const size_t Buffer::kCheapPrepend;
const size_t Buffer::kInitialSize;

ssize_t Buffer::readFd(int fd, int* savedErrno)
{
  // saved an ioctl()/FIONREAD call to tell how much to read
	// 在栈上准备一个65536字节的extrabuf,然后利用readv()来读取数据
  char extrabuf[65536];
	// iovec有两块
	// 第一块指向muduo buffer中的writable字节
	// 另一块指向栈上的extrabuf
  // 这样如果读入的数据不多,那么全部都读到Buffer里去
	// 如果长度超过了Buffer的writable字节数,就会读到栈上的extrabuf里
	// 然后程序再把extrabuf里的数据append()到buffer中
	// 这样利用了临时栈空间,避免每个链接的初始buffer过大造成内存浪费
	// 也避免反复调用read()的系统开销,因为缓冲区够大,一次readv()系统调用就能读完全部数据
	struct iovec vec[2];
  const size_t writable = writableBytes();
  vec[0].iov_base = begin()+writerIndex_;
  vec[0].iov_len = writable;
  vec[1].iov_base = extrabuf;
  vec[1].iov_len = sizeof extrabuf;
  // when there is enough space in this buffer, don't read into extrabuf.
  // when extrabuf is used, we read 128k-1 bytes at most.
  const int iovcnt = (writable < sizeof extrabuf) ? 2 : 1;
	// 总数据长度
  const ssize_t n = sockets::readv(fd, vec, iovcnt);
  if (n < 0)
  {
    *savedErrno = errno;
  }
  else if (implicit_cast<size_t>(n) <= writable)
  {
		// 这样如果读入的数据不多,那么全部都读到Buffer里去
    writerIndex_ += n;
  }
  else
  {
		// 如果长度超过了Buffer的writable字节数,就会读到栈上的extrabuf里
		// 然后程序再把extrabuf里的数据append()到buffer中
    writerIndex_ = buffer_.size();
    append(extrabuf, n - writable);
  }
  // if (n == writable + sizeof extrabuf)
  // {
  //   goto line_30;
  // }
  return n;
}

