// Copyright 2011, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_EXAMPLES_PROTOBUF_CODEC_DISPATCHER_LITE_H
#define MUDUO_EXAMPLES_PROTOBUF_CODEC_DISPATCHER_LITE_H

#include <muduo/base/noncopyable.h>
#include <muduo/net/Callbacks.h>

#include <google/protobuf/message.h>

#include <map>

typedef std::shared_ptr<google::protobuf::Message> MessagePtr;

class ProtobufDispatcherLite : muduo::noncopyable
{
 public:
  typedef std::function<void (const muduo::net::TcpConnectionPtr&,
                                const MessagePtr&,
                                muduo::Timestamp)> ProtobufMessageCallback;

  // ProtobufDispatcher()
  //   : defaultCallback_(discardProtobufMessage)
  // {
  // }

  explicit ProtobufDispatcherLite(const ProtobufMessageCallback& defaultCb)
    : defaultCallback_(defaultCb)
  {
  }

	// 由codec调用, 消息已经解码, 开始分发
  void onProtobufMessage(const muduo::net::TcpConnectionPtr& conn,
                         const MessagePtr& message,
                         muduo::Timestamp receiveTime) const
  {
		// 获取消息的Descriptor, 从map里找回调
    CallbackMap::const_iterator it = callbacks_.find(message->GetDescriptor());
    if (it != callbacks_.end())
    {
			// 调用回调
			// 一个缺陷, 这个message是MessagePtr类型的, 客户代码只能接受基类, 需要自己做向下转型
      it->second(conn, message, receiveTime);
    }
    else
    {
			// 找不到就调用default的
      defaultCallback_(conn, message, receiveTime);
    }
  }

	// 注册回调, 存入map, key是Descriptor, value是callback
	// 在非lite版里, 这里用模板
  void registerMessageCallback(const google::protobuf::Descriptor* desc,
                               const ProtobufMessageCallback& callback)
  {
    callbacks_[desc] = callback;
  }

 private:
  // static void discardProtobufMessage(const muduo::net::TcpConnectionPtr&,
  //                                    const MessagePtr&,
  //                                    muduo::Timestamp);

	 // 这个map里负责保存回调
	 // key是Descriptor, 每个消息都有一个对应的全局对象, 地址不变, 唯一key
  typedef std::map<const google::protobuf::Descriptor*, ProtobufMessageCallback> CallbackMap;
  CallbackMap callbacks_;
  ProtobufMessageCallback defaultCallback_;
};

#endif  // MUDUO_EXAMPLES_PROTOBUF_CODEC_DISPATCHER_LITE_H

