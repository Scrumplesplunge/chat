#pragma once

#include "message_type.h"

#include <functional>
#include <mutex>
#include <unordered_map>
#include <scrump/binary.h>
#include <scrump/data_node.h>
#include <scrump/json.h>
#include <scrump/socket.h>
#include <string>
#include <vector>

namespace network {

template <typename T> scrump::DataNode encode(const T& message);
template <typename T> void decode(const scrump::DataNode& input, T* message);

}  // namespace network

template <MessageType message_type>
struct Message {
  static const MessageType type = message_type;
};

#define DECLARE_MESSAGE(name)  \
  namespace network {  \
    template <> scrump::DataNode encode(const Message<name>& message);  \
    template <> void decode(  \
        const scrump::DataNode& input, Message<name>* message);  \
  }  \
  namespace scrump {  \
    template <> void BinaryReader::read(Message<name>* message);  \
    template <> void BinaryWriter::write(const Message<name>& message);  \
  }  \
  template <> struct Message<name>

DECLARE_MESSAGE(IDENTIFY) {
  std::string display_name;
};

DECLARE_MESSAGE(SEND_MESSAGE) {
  std::string text;
};

DECLARE_MESSAGE(RECEIVE_MESSAGE) {
  enum Category {
    CHAT_MESSAGE = 0,  // Sent by a user.
    NOTICE = 1,        // Sent by the server.
  };

  uint64_t message_id;  // Unique ID of this message.

  Category category;
  std::string sender_name;  // Populated only if category is CHAT_MESSAGE.
  std::string text;
};
typedef Message<RECEIVE_MESSAGE> ChatMessage;

DECLARE_MESSAGE(REQUEST_HISTORY) {
  uint64_t start_id;
  uint64_t num_messages;
};

DECLARE_MESSAGE(RECEIVE_HISTORY) {
  std::vector<ChatMessage> messages;
};

#undef DECLARE_MESSAGE

class BinaryConnection {
 public:
  BinaryConnection(scrump::Socket socket);

  template <MessageType message_type>
  void send(const Message<message_type>& message) {
    // Send the message in binary form.
    scrump::BinaryWriter writer(socket_);
    writer.writeVarUint(static_cast<uint64_t>(message_type));
    writer.writeString(scrump::serialize(message));
  }

  template <MessageType message_type>
  void on(std::function<void(Message<message_type>&& message)> callback) {
    // Parse the binary payload and run the callback.
    callbacks_.emplace(message_type, [callback](const std::string& data) {
      callback(scrump::deserialize<Message<message_type>>(data));
    });
  }

  void poll();

 private:
  typedef std::function<void(const std::string&)> Handler;

  scrump::Socket socket_;
  std::unordered_map<MessageType, Handler> callbacks_;
};

class JSONConnection {
 public:
  JSONConnection(scrump::Socket socket);

  template <MessageType message_type>
  void send(const Message<message_type>& message) {
    send(message_type, network::encode(message));
  }

  template <MessageType message_type>
  void on(std::function<void(Message<message_type>&& message)> callback) {
    // Parse the JSON object and run the callback.
    callbacks_.emplace(
        message_type, [callback](const scrump::DataNode& payload) {
      Message<message_type> message;
      network::decode(payload, &message);
      callback(std::move(message));
    });
  }

  void poll();

 private:
  void send(MessageType message_type, scrump::DataNode object);

  typedef std::function<void(const scrump::DataNode&)> Handler;

  scrump::Socket socket_;
  std::unordered_map<MessageType, Handler> callbacks_;
};

class Connection {
 public:
  enum Mode {
    BINARY,
    JSON,
  };

  Connection(scrump::Socket socket);  // Server side.
  Connection(Mode mode, scrump::Socket socket);  // Client side.
  ~Connection();

  template <MessageType message_type>
  void send(const Message<message_type>& message) {
    std::unique_lock<std::mutex> lock(writer_mutex_);
    switch (mode_) {
      case BINARY: return binary_connection_.send(message);
      case JSON: return json_connection_.send(message);
    }
  }

  template <MessageType message_type>
  void on(std::function<void(Message<message_type>&& message)> callback) {
    std::unique_lock<std::mutex> lock(callback_mutex_);
    switch (mode_) {
      case BINARY: return binary_connection_.on(callback);
      case JSON: return json_connection_.on(callback);
    }
  }

  void poll();
    
 private:
  Mode mode_;

  std::mutex reader_mutex_, writer_mutex_, callback_mutex_;

  union {
    BinaryConnection binary_connection_;
    JSONConnection json_connection_;
  };
};
