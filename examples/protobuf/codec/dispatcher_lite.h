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

	// ��codec����, ��Ϣ�Ѿ�����, ��ʼ�ַ�
  void onProtobufMessage(const muduo::net::TcpConnectionPtr& conn,
                         const MessagePtr& message,
                         muduo::Timestamp receiveTime) const
  {
		// ��ȡ��Ϣ��Descriptor, ��map���һص�
    CallbackMap::const_iterator it = callbacks_.find(message->GetDescriptor());
    if (it != callbacks_.end())
    {
			// ���ûص�
			// һ��ȱ��, ���message��MessagePtr���͵�, �ͻ�����ֻ�ܽ��ܻ���, ��Ҫ�Լ�������ת��
      it->second(conn, message, receiveTime);
    }
    else
    {
			// �Ҳ����͵���default��
      defaultCallback_(conn, message, receiveTime);
    }
  }

	// ע��ص�, ����map, key��Descriptor, value��callback
	// �ڷ�lite����, ������ģ��
  void registerMessageCallback(const google::protobuf::Descriptor* desc,
                               const ProtobufMessageCallback& callback)
  {
    callbacks_[desc] = callback;
  }

 private:
  // static void discardProtobufMessage(const muduo::net::TcpConnectionPtr&,
  //                                    const MessagePtr&,
  //                                    muduo::Timestamp);

	 // ���map�︺�𱣴�ص�
	 // key��Descriptor, ÿ����Ϣ����һ����Ӧ��ȫ�ֶ���, ��ַ����, Ψһkey
  typedef std::map<const google::protobuf::Descriptor*, ProtobufMessageCallback> CallbackMap;
  CallbackMap callbacks_;
  ProtobufMessageCallback defaultCallback_;
};

#endif  // MUDUO_EXAMPLES_PROTOBUF_CODEC_DISPATCHER_LITE_H

