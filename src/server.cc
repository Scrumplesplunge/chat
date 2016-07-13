#include "network.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <queue>
#include <scrump/args.h>
#include <scrump/logging.h>
#include <thread>

using namespace scrump;
using namespace std;

OPTION(string, host, "0.0.0.0", "Host address to bind to.");
OPTION(int, port, 17994, "Port to bind to.");

typedef map<uint64_t, ChatMessage> Messages;

typedef string Address;
typedef string Username;

struct User {
  User(Socket socket);

  mutex name_mutex;
  string display_name;

  Connection connection;
};

User::User(Socket socket)
    : display_name(socket.hostPort()), connection(move(socket)) {}

typedef map<Address, User*> Users;

class Server {
 public:
  void run();

  void serve(Socket socket);

  void notify(string message);
  void send(string sender, string text);

 private:
  void addMessage(ChatMessage&& message);

  mutex message_mutex_;
  uint64_t next_id_ = 0;
  Messages messages_;

  mutex users_mutex_;
  Users users_;
};

void Server::run() {
  LOG(VERBOSE) << "Binding to " << options::host << ":" << options::port;
  Socket socket;
  socket.bind(options::host, options::port);

  LOG(VERBOSE) << "Listening for incoming connections..";
  socket.listen();

  LOG(INFO) << "Server started on " << options::host << ":" << options::port;
  while (true) thread(&Server::serve, this, socket.accept()).detach();
}

void Server::notify(string text) {
  ChatMessage message;
  message.category = ChatMessage::NOTICE;
  message.text = move(text);

  addMessage(move(message));
}

void Server::send(string sender, string text) {
  ChatMessage message;
  message.category = ChatMessage::CHAT_MESSAGE;
  message.sender_name = move(sender);
  message.text = move(text);

  addMessage(move(message));
}

void Server::addMessage(ChatMessage&& message) {
  // Store the message in the messages list.
  unique_lock<mutex> message_lock(message_mutex_);
  
  uint64_t message_id = message.message_id = next_id_++;

  // Forward the message to all connected users.
  unique_lock<mutex> users_lock(users_mutex_);
  for (auto& user : users_) user.second->connection.send(message);
  
  // Store the message in the message history.
  messages_.emplace(message_id, move(message));
}

void Server::serve(Socket socket) {
  // Read the connection header.
  LOG(INFO) << "Accepted incoming connection from " << socket.hostPort();
  notify(socket.hostPort() + " has connected.");

  // Create the user struct.
  string address = socket.hostPort();
  User user(move(socket));
  {
    unique_lock<mutex> users_lock(users_mutex_);
    users_.emplace(address, &user);
  }

  user.connection.on<IDENTIFY>([this, &user](Message<IDENTIFY>&& message) {
    // Update the stored name.
    string old_name, new_name;
    {
      unique_lock<mutex> lock(user.name_mutex);
      old_name = move(user.display_name);
      new_name = message.display_name;
      user.display_name = move(message.display_name);
    }

    // Send the name update message.
    notify(old_name + " is now known as " + new_name + ".");
  });

  user.connection.on<SEND_MESSAGE>(
      [this, &user](Message<SEND_MESSAGE>&& message) {
    // Fetch the user display name.
    string sender;
    {
      unique_lock<mutex> lock(user.name_mutex);
      sender = user.display_name;
    }

    // Send the message.
    send(move(sender), move(message.text));
  });

  user.connection.on<REQUEST_HISTORY>(
      [this, &user](Message<REQUEST_HISTORY>&& message) {
    // Lock the message list and extract the requested messages.
    Message<RECEIVE_HISTORY> history;
    {
      unique_lock<mutex> lock(message_mutex_);
      auto i = messages_.lower_bound(message.start_id);
      
      while (history.messages.size() < message.num_messages &&
             i != messages_.end()) {
        history.messages.push_back(i->second);
        i++;
      }
    }

    // Send the response.
    user.connection.send(history);
  });

  try {
    while (true) user.connection.poll();
  } catch (const exception& error) {
    // Remove the user from the users list.
    {
      unique_lock<mutex> users_lock(users_mutex_);
      users_.erase(address);
    }

    // Notify the other users.
    string name;
    {
      unique_lock<mutex> user_lock(user.name_mutex);
      name = user.display_name;
    }

    LOG(ERROR) << "Exception thrown in connection to " << address << ": "
               << error.what();
    notify(name + " forcefully disconnected (an exception was thrown).");
    return;
  }
}

int scrump_main(int argc, char* args[]) {
  Server server;

  server.run();

  return 0;
}
