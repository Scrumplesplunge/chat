#include "network.h"

#include <scrump/data_node.h>
#include <scrump/logging.h>
#include <scrump/json.h>
#include <stdexcept>

using namespace std;
using namespace scrump;

#define ENCODER(name)  \
  template <> scrump::DataNode network::encode(const Message<name>& message)
#define DECODER(name)  \
  template <> void network::decode(  \
      const DataNode& input, Message<name>* message)
#define READER(name)  \
  template <> void scrump::BinaryReader::read(Message<name>* message)
#define WRITER(name)  \
  template <> void scrump::BinaryWriter::write(const Message<name>& message)

// IDENTIFY
ENCODER(IDENTIFY) {
  return DataNode::Object{{"display_name", message.display_name}};
}

DECODER(IDENTIFY) {
  message->display_name = input.asObject()["display_name"].asString();
}

READER(IDENTIFY) {
  message->display_name = readString();
}

WRITER(IDENTIFY) {
  writeString(message.display_name);
}

// SEND_MESSAGE
ENCODER(SEND_MESSAGE) {
  return DataNode::Object{{"text", message.text}};
}

DECODER(SEND_MESSAGE) {
  message->text = input.asObject()["text"].asString();
}

READER(SEND_MESSAGE) {
  message->text = readString();
}

WRITER(SEND_MESSAGE) {
  writeString(message.text);
}

// RECEIVE_MESSAGE
ENCODER(RECEIVE_MESSAGE) {
  // Convert the category into a string.
  string category;
  switch (message.category) {
    case ChatMessage::CHAT_MESSAGE:
      return DataNode::Object{
          {"message_id", static_cast<int64_t>(message.message_id)},
          {"category", "CHAT_MESSAGE"},
          {"sender_name", message.sender_name},
          {"text", message.text}};
    case ChatMessage::NOTICE:
      return DataNode::Object{
          {"message_id", static_cast<int64_t>(message.message_id)},
          {"category", "NOTICE"},
          {"text", message.text}};
    default:
      throw runtime_error("Bad message category.");
  }
}

DECODER(RECEIVE_MESSAGE) {
  DataNode::Object& node = input.asObject();
  message->message_id = node["message_id"].asInt64();
  // Parse the category from its string form.
  string category = node["category"].asString();
  if (category == "CHAT_MESSAGE") {
    message->category = ChatMessage::CHAT_MESSAGE;
    message->sender_name = node["sender_name"].asString();
  } else if (category == "NOTICE") {
    message->category = ChatMessage::NOTICE;
  } else {
    throw runtime_error("Invalid message category: " + category);
  }
  message->text = node["text"].asString();
}

READER(RECEIVE_MESSAGE) {
  message->message_id = readVarUint();
  message->category = static_cast<ChatMessage::Category>(readVarUint());
  if (message->category == ChatMessage::CHAT_MESSAGE) {
    message->sender_name = readString();
  } else {
    message->sender_name = "";
  }
  message->text = readString();
}

WRITER(RECEIVE_MESSAGE) {
  writeVarUint(message.message_id);
  writeVarUint(static_cast<uint64_t>(message.category));
  if (message.category == ChatMessage::CHAT_MESSAGE)
    writeString(message.sender_name);
  writeString(message.text);
}

// REQUEST_HISTORY
ENCODER(REQUEST_HISTORY) {
  return DataNode::Object{
      {"start_id", static_cast<int64_t>(message.start_id)},
      {"num_messages", static_cast<int64_t>(message.num_messages)}};
}

DECODER(REQUEST_HISTORY) {
  DataNode::Object& node = input.asObject();
  message->start_id = node["start_id"].asInt64();
  message->num_messages = node["num_messages"].asInt64();
}

READER(REQUEST_HISTORY) {
  message->start_id = readVarUint();
  message->num_messages = readVarUint();
}

WRITER(REQUEST_HISTORY) {
  writeVarUint(message.start_id);
  writeVarUint(message.num_messages);
}

// RECEIVE_HISTORY
ENCODER(RECEIVE_HISTORY) {
  DataNode::Array output;
  for (const ChatMessage& entry : message.messages)
    output.push_back(encode(entry));
  return output;
}

DECODER(RECEIVE_HISTORY) {
  message->messages.clear();
  DataNode::Array& messages = input.asArray();
  for (const DataNode& node : messages) {
    ChatMessage temp;
    decode(node, &temp);
    message->messages.push_back(move(temp));
  }
}

READER(RECEIVE_HISTORY) {
  uint64_t num_messages = readVarUint();
  for (uint64_t i = 0; i < num_messages; i++) {
    ChatMessage temp;
    read(&temp);
    message->messages.push_back(temp);
  }
}

WRITER(RECEIVE_HISTORY) {
  writeVarUint(message.messages.size());
  for (const ChatMessage& entry : message.messages) write(entry);
}

BinaryConnection::BinaryConnection(Socket socket)
    : socket_(move(socket)) {}

void BinaryConnection::poll() {
  BinaryReader reader(socket_);

  // Receive the message.
  MessageType type = static_cast<MessageType>(reader.readVarUint());
  string data = reader.readString();

  // Check whether there is a handler for this message type.
  auto i = callbacks_.find(type);
  if (i == callbacks_.end()) {
    LOG(WARNING) << "No handler for incoming message of type "
                 << toString(type);
    return;
  }

  // Invoke the handler.
  i->second(data);
}

JSONConnection::JSONConnection(Socket socket)
    : socket_(move(socket)) {}

static void discard(const string& data) {
  LOG(ERROR) << "Severing connection due to bad message: " << data;
  throw runtime_error("Bad message from client.");
}

void JSONConnection::poll() {
  string data;
  char c = 0;

  // Receive the message.
  while (socket_.receive(&c, 1) && c != '\n') data.push_back(c);
  if (c != '\n') throw socket_error("Connection severed.");

  // Decode the message.
  MessageType type;
  DataNode payload;
  {
    DataNode node;
    try {
      node = JSON::parse(data);
    } catch (...) {
      return discard(data);
    }

    // Check that the message is an object.
    if (node.type() != DataNode::OBJECT) return discard(data);
    DataNode::Object& object = node.asObject();

    // Check that there is a type field and it is a valid string value.
    auto i = object.find("type");
    if (i == object.end()) return discard(data);
    DataNode& type_node = i->second;
    if (type_node.type() != DataNode::STRING) return discard(data);
    if (!fromString(type_node.asString(), &type)) return discard(data);

    // Check that there is a payload field and extract it.
    i = object.find("payload");
    if (i == object.end()) return discard(data);
    payload = move(i->second);
  }

  // Check whether there is a handler for this message type.
  auto i = callbacks_.find(type);
  if (i == callbacks_.end()) {
    LOG(WARNING) << "No handler for incoming message of type "
                 << toString(type);
    return;
  }

  // Invoke the handler.
  i->second(payload);
}

void JSONConnection::send(MessageType message_type, DataNode object) {
  DataNode node = scrump::DataNode::Object{
      {"type", toString(message_type)},
      {"payload", move(object)}};
  socket_.send(scrump::JSON::stringify(node) + "\n");
}

static string readline(Socket& socket) {
  string data;
  char c;
  while (socket.receive(&c, 1) && c != '\n') data.push_back(c);
  return data;
}

Connection::Connection(Socket socket) {
  {
    string mode_string = readline(socket);
    if (mode_string == "BINARY") {
      mode_ = Connection::BINARY;
    } else if (mode_string == "JSON") {
      mode_ = Connection::JSON;
    } else {
      LOG(ERROR) << "Invalid connection mode. Aborting.";
      socket.send("Invalid connection type.");
      return;
    }
  }
  LOG(INFO) << "Connection mode is "
            << (mode_ == Connection::BINARY ? "BINARY" : "JSON");
  switch (mode_) {
    case BINARY:
      new(&binary_connection_) BinaryConnection(move(socket));
      break;
    case JSON:
      new(&json_connection_) JSONConnection(move(socket));
      break;
  }
}

Connection::Connection(Mode mode, Socket socket)
    : mode_(mode) {
  switch (mode) {
    case BINARY:
      socket.send("BINARY\n");
      new(&binary_connection_) BinaryConnection(move(socket));
      break;
    case JSON:
      socket.send("JSON\n");
      new(&json_connection_) JSONConnection(move(socket));
      break;
  }
}

Connection::~Connection() {
  switch (mode_) {
    case BINARY: binary_connection_.~BinaryConnection(); break;
    case JSON: json_connection_.~JSONConnection(); break;
  }
}

void Connection::poll() {
  unique_lock<mutex> lock(reader_mutex_);
  switch (mode_) {
    case BINARY: return binary_connection_.poll();
    case JSON: return json_connection_.poll();
  }
}
