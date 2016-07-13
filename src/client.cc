#include "network.h"

#include <map>
#include <mutex>
#include <scrump/args.h>
#include <scrump/atomic_output.h>
#include <scrump/color.h>
#include <thread>
#include <unistd.h>

using namespace scrump;
using namespace std;

OPTION(string, host, "0.0.0.0", "Host address to connect to.");
OPTION(int, port, 17994, "Port to connect to.");
OPTION(string, display_name, "",
       "Display name to use. If unset, defaults to username.");

const Color NOTICE_COLOR = Color::GREEN;
const Color NAME_COLOR = Color::CYAN;
const Color PROMPT_COLOR = Color::YELLOW;
const Color ERROR_COLOR = Color::RED;

void displayMessage(const ChatMessage& message) {
  switch (message.category) {
    case ChatMessage::NOTICE:
      aout << NOTICE_COLOR << message.text << Color::RESET << "\n";
      break;
    case ChatMessage::CHAT_MESSAGE:
      aout << NAME_COLOR << message.sender_name << ": " << Color::RESET
           << message.text << "\n";
      break;
  }
}

int main(int argc, char* args[]) {
  args::process(&argc, args);

  // Connect to the server.
  Socket socket;
  socket.connect(options::host, options::port);
  Connection connection(Connection::BINARY, move(socket));

  // Identify.
  string display_name = options::display_name;
  if (display_name == "") {
    // Display name is unset. Default to the linux username.
    const int MAX_LENGTH = 256;
    char temp[MAX_LENGTH];
    getlogin_r(temp, MAX_LENGTH);
    display_name = string(temp);
  }
  {
    // Send the identification message.
    Message<IDENTIFY> message;
    message.display_name = display_name;
    connection.send(message);
  }

  // Set up the message handlers.
  mutex message_mutex;
  uint64_t last_seen = 0;
  uint64_t new_messages = 0;
  map<uint64_t, ChatMessage> messages;

  connection.on<RECEIVE_MESSAGE>([&](Message<RECEIVE_MESSAGE>&& message) {
    uint64_t id = message.message_id;

    unique_lock<mutex> lock(message_mutex);
    new_messages++;
    messages.emplace(id, move(message));
  });

  connection.on<RECEIVE_HISTORY>([&](Message<RECEIVE_HISTORY>&& history) {
    unique_lock<mutex> lock(message_mutex);
    for (ChatMessage& message : history.messages) {
      uint64_t id = message.message_id;
      messages.emplace(id, move(message));
    }
  });

  // Start the message sending thread.
  thread input([&] {
    string line;
    Message<SEND_MESSAGE> message;
    aout << PROMPT_COLOR << "[] " << Color::RESET;
    while (getline(cin, line)) {
      string command, argument;
      {
        auto split = line.find_first_of(" ");
        if (split == string::npos) {
          command = line;
        } else {
          command = line.substr(0, split);
          argument = line.substr(split + 1);
        }
      }
      if (command == "m") {
        // Message.
        message.text = move(argument);
        connection.send(message);
      } else if (command == "v") {
        // View.
        unique_lock<mutex> lock(message_mutex);
        auto i = messages.lower_bound(last_seen + 1);
        while (i != messages.end()) {
          displayMessage(i->second);
          last_seen = i->first;
          i++;
        }
        new_messages = 0;
      }
      unique_lock<mutex> lock(message_mutex);
      if (new_messages > 0) {
        aout << PROMPT_COLOR << "[" << NOTICE_COLOR << new_messages
             << PROMPT_COLOR << "] " << Color::RESET;
      } else {
        aout << PROMPT_COLOR << "[] " << Color::RESET;
      }
    }
  });

  // Repeatedly handle incoming messages.
  while (true) connection.poll();
}
