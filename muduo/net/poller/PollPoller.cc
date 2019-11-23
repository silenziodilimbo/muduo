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

// ��EventLoop::loop��ѭ���е���
Timestamp PollPoller::poll(int timeoutMs, ChannelList* activeChannels)
{
  // XXX pollfds_ shouldn't change
  // ͨ��poll��ѯ�Ƿ��л�Ծ���ļ�������
  int numEvents = ::poll(&*pollfds_.begin(), pollfds_.size(), timeoutMs);
  int savedErrno = errno;
  Timestamp now(Timestamp::now());
  if (numEvents > 0)
  {
    // ����ֵ����0��ʾ��Ӧ������Ծ���ļ�������
    // ���ú�������ȡ��Ծ���ļ���������
    LOG_TRACE << numEvents << " events happened";
    fillActiveChannels(numEvents, activeChannels);
  }
  else if (numEvents == 0)
  {
    //���κλ�Ծ���ļ�������
    LOG_TRACE << " nothing happened";
  }
  else
  {
    //����
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
  // ����pollfds_
  for (PollFdList::const_iterator pfd = pollfds_.begin();
      pfd != pollfds_.end() && numEvents > 0; ++pfd)
  {
    // ѭ��ȡֵ
    if (pfd->revents > 0)
    {
      // �ҳ��л�¼���fd, ������Ӧ��channel ���� activeChannels
      // ���¼�����ʱ������ϵͳ���struct pollfd��revents��ֵ��ֵ����0
      --numEvents; // Ч��
      ChannelMap::const_iterator ch = channels_.find(pfd->fd);
      assert(ch != channels_.end());
      Channel* channel = ch->second;
      assert(channel->fd() == pfd->fd);
      channel->set_revents(pfd->revents);
      // pfd->revents = 0;
      activeChannels->push_back(channel);
    }
  }
  // ע��
  // ���ﲻ�ܵ���ʵ�ʵ�Handler, ��ΪHandler���ܻ���ӻ�ɾ��CHannel, �����ƻ�����, ��Σ��
  // Ҳ����Ϊ��һְ��ԭ��, ֻIO, ���ַ�
}

// ĳ��Channel�����update
// ʵ�����ǵ�����EventLoop::updateChannel�������Լ�, ��channel
// �������������Poller::updateChannel����, ������channel
// ������µ�Channel (û��index)
// ����һ��pollfd��������, ����fd/events/revents
// ��pollfd�ŵ�������
// ����һ��channel��index, ��ֵ��channel, �������
// ��channel�ŵ�������
// ��������µ�channel (�Ѿ�����index)
// ȡ��pfd�����и���
// ���ĳ��Channel��ʱ�������κ��¼�, ����Ϊ-1, ��poll���Դ���
void PollPoller::updateChannel(Channel* channel)
{
  Poller::assertInLoopThread();
  LOG_TRACE << "fd = " << channel->fd() << " events = " << channel->events();
  if (channel->index() < 0)
  {
    // a new one, add to pollfds_
    // indexС��0��ʾ��һ���µ�Channel��
    // ��ӵ��������
    assert(channels_.find(channel->fd()) == channels_.end());
    struct pollfd pfd;
    pfd.fd = channel->fd();
    pfd.events = static_cast<short>(channel->events());
    pfd.revents = 0;
    pollfds_.push_back(pfd);
    int idx = static_cast<int>(pollfds_.size())-1;
    channel->set_index(idx);
    channels_[pfd.fd] = channel; // channel��fd��, ����Map��
  }
  else
  {
    // update existing one
    // index����0��ʾ�����д��ڴ�ͨ�������Խ��и��¡�
    assert(channels_.find(channel->fd()) != channels_.end()); // У��
    assert(channels_[channel->fd()] == channel); // У��
    int idx = channel->index();
    assert(0 <= idx && idx < static_cast<int>(pollfds_.size()));
    struct pollfd& pfd = pollfds_[idx];
    assert(pfd.fd == channel->fd() || pfd.fd == -channel->fd()-1);
    pfd.fd = channel->fd();
    pfd.events = static_cast<short>(channel->events()) // �û������¼�����;
    pfd.revents = 0;
    if (channel->isNoneEvent())
    {
      // ignore this pollfd
      // ���ĳ��Channel��ʱ�������κ��¼�, ����Ϊ-1, ��poll���Դ���
      // ����Ϊʲô�������ã����Ļ����
      pfd.fd = -channel->fd()-1;
    }
  }
}

// �Ƴ�ͨ��
// ��Channel::remove����
// ʵ�����ǵ�����EventLoop::removeChannel
// �������������Poller::removeChannel����
// ��Channel��ȡ��index
// �б���ɾ��fd
// �б���ɾ��channel
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
    //���index�������б�Ľ�β����ֱ��pop�ͷš�
    pollfds_.pop_back();
  }
  else
  {
    //���ɾ����struct pollfd�����б�Ľ�β��
    //����Ч�ʿ��ǣ�ֱ�ӽ�Ҫɾ����pollfd��β����
    //pollfd������Ȼ���ͷ�
    int channelAtEnd = pollfds_.back().fd;
    iter_swap(pollfds_.begin()+idx, pollfds_.end()-1);
    if (channelAtEnd < 0)
    {
      //��Ϊ��updateChannelʱ���ǽ�ͨ������ȡ��-1��
      //��������ԭ��ȡ�ء�
      channelAtEnd = -channelAtEnd-1;
    }
    channels_[channelAtEnd]->set_index(idx);
    pollfds_.pop_back();
  }
}

