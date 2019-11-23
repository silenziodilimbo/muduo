// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/poller/PollPoller.h>

#include <muduo/base/Logging.h>
#include <muduo/base/Types.h>
#include <muduo/net/Channel.h>

#include <assert.h>
#include <errno.h>
#include <poll.h>

using namespace muduo;
using namespace muduo::net;

PollPoller::PollPoller(EventLoop* loop)
  : Poller(loop)
{
}

PollPoller::~PollPoller() = default;

// 在EventLoop::loop的循环中调用
Timestamp PollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
  // XXX pollfds_ shouldn't change
  // 通过poll查询是否有活跃的文件描述符
  int numEvents = ::poll(&*pollfds_.begin(), pollfds_.size(), timeoutMs);
  int savedErrno = errno;
  Timestamp now(Timestamp::now());
  if (numEvents > 0)
  {
    // 返回值大于0表示对应个数活跃的文件描述符
    // 调用函数，获取活跃的文件描述符。
    LOG_TRACE << numEvents << " events happened";
    fillActiveChannels(numEvents, activeChannels);
  }
  else if (numEvents == 0)
  {
    //无任何活跃的文件描述符
    LOG_TRACE << " nothing happened";
  }
  else
  {
    //出错
    if (savedErrno != EINTR)
    {
      errno = savedErrno;
      LOG_SYSERR << "PollPoller::poll()";
    }
  }
  return now;
}

void PollPoller::fillActiveChannels(int numEvents,
                                    ChannelList* activeChannels) const
{
  // 遍历pollfds_
  for (PollFdList::const_iterator pfd = pollfds_.begin();
      pfd != pollfds_.end() && numEvents > 0; ++pfd)
  {
    // 循环取值
    if (pfd->revents > 0)
    {
      // 找出有活动事件的fd, 把它对应的channel 填入 activeChannels
      // 有事件发生时，操作系统会给struct pollfd的revents赋值，值大于0
      --numEvents; // 效率
      ChannelMap::const_iterator ch = channels_.find(pfd->fd);
      assert(ch != channels_.end());
      Channel* channel = ch->second;
      assert(channel->fd() == pfd->fd);
      channel->set_revents(pfd->revents);
      // pfd->revents = 0;
      activeChannels->push_back(channel);
    }
  }
  // 注意
  // 这里不能调用实际的Handler, 因为Handler可能会添加或删除CHannel, 进而破坏遍历, 很危险
  // 也是因为单一职责原则, 只IO, 不分发
}

// 某个Channel会调用update
// 实际上是调用了EventLoop::updateChannel来更新自己, 即channel
// 这个函数调用了Poller::updateChannel方法, 来更新channel
// 如果是新的Channel (没有index)
// 创建一个pollfd来管理它, 包括fd/events/revents
// 把pollfd放到队列中
// 生成一个channel的index, 赋值给channel, 用作标记
// 把channel放到队列中
// 如果不是新的channel (已经有了index)
// 取出pfd来进行更新
// 如果某个Channel暂时不关心任何事件, 就置为-1, 让poll忽略此项
void PollPoller::updateChannel(Channel* channel)
{
  Poller::assertInLoopThread();
  LOG_TRACE << "fd = " << channel->fd() << " events = " << channel->events();
  if (channel->index() < 0)
  {
    // a new one, add to pollfds_
    // index小于0表示是一个新的Channel，
    // 添加到管理队列
    assert(channels_.find(channel->fd()) == channels_.end());
    struct pollfd pfd;
    pfd.fd = channel->fd();
    pfd.events = static_cast<short>(channel->events());
    pfd.revents = 0;
    pollfds_.push_back(pfd);
    int idx = static_cast<int>(pollfds_.size())-1;
    channel->set_index(idx);
    channels_[pfd.fd] = channel; // channel和fd绑定, 存入Map中
  }
  else
  {
    // update existing one
    // index大于0表示队列中存在此通道，所以进行更新。
    assert(channels_.find(channel->fd()) != channels_.end()); // 校验
    assert(channels_[channel->fd()] == channel); // 校验
    int idx = channel->index();
    assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
    struct pollfd& pfd = pollfds_[idx];
    assert(pfd.fd == channel->fd() || pfd.fd == -channel->fd()-1);
    pfd.fd = channel->fd();
    pfd.events = static_cast<short>(channel->events()) // 用户请求事件保存;
    pfd.revents = 0;
    if (channel->isNoneEvent())
    {
      // ignore this pollfd
      // 如果某个Channel暂时不关心任何事件, 就置为-1, 让poll忽略此项
      // 至于为什么至于设置，下文会解释
      pfd.fd = -channel->fd()-1;
    }
  }
}

// 移除通道
// 由Channel::remove调用
// 实际上是调用了EventLoop::removeChannel
// 这个函数调用了Poller::removeChannel方法
// 从Channel中取出index
// 列表中删除fd
// 列表中删除channel
void PollPoller::removeChannel(Channel* channel)
{
  Poller::assertInLoopThread();
  LOG_TRACE << "fd = " << channel->fd();
  assert(channels_.find(channel->fd()) != channels_.end());
  assert(channels_[channel->fd()] == channel);
  assert(channel->isNoneEvent());
  int idx = channel->index();
  assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
  const struct pollfd& pfd = pollfds_[idx]; (void)pfd;
  assert(pfd.fd == -channel->fd()-1 && pfd.events == channel->events());
  size_t n = channels_.erase(channel->fd());
  assert(n == 1); (void)n;
  if (implicit_cast<size_t>(idx) == pollfds_.size()-1)
  {
    //如果index正好是列表的结尾，则直接pop释放。
    pollfds_.pop_back();
  }
  else
  {
    //如果删除的struct pollfd不是列表的结尾，
    //出于效率考虑，直接将要删除的pollfd与尾部的
    //pollfd交换，然后释放
    int channelAtEnd = pollfds_.back().fd;
    iter_swap(pollfds_.begin()+idx, pollfds_.end()-1);
    if (channelAtEnd < 0)
    {
      //因为在updateChannel时，是将通道进行取反-1，
      //所以这里原样取回。
      channelAtEnd = -channelAtEnd-1;
    }
    channels_[channelAtEnd]->set_index(idx);
    pollfds_.pop_back();
  }
}

