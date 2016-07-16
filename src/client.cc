#include "network.h"

#include <map>
#include <mutex>
#include <readline/readline.h>
#include <scrump/args.h>
#include <scrump/atomic_output.h>
#include <scrump/color.h>
#include <sstream>
#include <string>
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

// Saves input temporarily, allowing output above the input prompt.
class ReadlineSaver {
 public:
  ReadlineSaver() {
    saved_point_ = rl_point;
    saved_line_ = rl_copy_text(0, rl_end);
    rl_save_prompt();
    rl_replace_line("", 0);
    rl_redisplay();
  }

  ~ReadlineSaver() {
    rl_restore_prompt();
    rl_replace_line(saved_line_, 0);
    rl_point = saved_point_;
    rl_redisplay();
    free(saved_line_);
  }

 private:
  int saved_point_;
  char* saved_line_;
};

class Output {
 public:
  ~Output() {
    cout << "\n";
  }

  template <typename T>
  Output& operator<<(T value) {
    cout << value;
    return *this;
  }

 private:
  ReadlineSaver input_saver;
};

void displayMessage(const ChatMessage& message) {
  switch (message.category) {
    case ChatMessage::NOTICE:
      Output() << NOTICE_COLOR << message.text << Color::RESET;
      break;
    case ChatMessage::CHAT_MESSAGE:
      Output() << NAME_COLOR << message.sender_name << Color::RESET << ": "
               << message.text;
      break;
  }
}

char* input(const string& prompt_text) {
  stringstream prompt;
  prompt << PROMPT_COLOR << prompt_text << Color::RESET << "> ";
  char* output = readline(prompt.str().c_str());
  // Move the cursor back up to the input line and clear it.
  cout << "\x1B[A;\r\x1B[K;" << flush;
  return output;
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
  connection.on<RECEIVE_MESSAGE>([](Message<RECEIVE_MESSAGE>&& message) {
    displayMessage(message);
  });

  // Start the message sending thread.
  thread input_thread([&] {
    Message<SEND_MESSAGE> message;
    char* line = input(display_name);
    while (line) {
      message.text = line;
      connection.send(message);
      free(line);
      line = input(display_name);
    }
  });

  // Repeatedly handle incoming messages.
  while (true) connection.poll();
}
