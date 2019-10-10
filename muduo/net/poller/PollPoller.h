// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_POLLER_POLLPOLLER_H
#define MUDUO_NET_POLLER_POLLPOLLER_H

#include <muduo/net/Poller.h>

#include <vector>

struct pollfd;

namespace muduo
{
namespace net
{

///
/// IO Multiplexing with poll(2).
///
class PollPoller : public Poller
{
 public:

  PollPoller(EventLoop* loop);
  ~PollPoller() override;

  // 查询出活跃的通道
  Timestamp poll(int timeoutMs, ChannelList* activeChannels) override;
  // 更新通道
  void updateChannel(Channel* channel) override;
  // 移除通道，一般在调用此方法之前，都会调用Channel->disableAll()
  void removeChannel(Channel* channel) override;

 private:
  void fillActiveChannels(int numEvents,
                          ChannelList* activeChannels) const;

  typedef std::vector<struct pollfd> PollFdList;
  PollFdList pollfds_; //fd的集合
};

}  // namespace net
}  // namespace muduo
#endif  // MUDUO_NET_POLLER_POLLPOLLER_H
