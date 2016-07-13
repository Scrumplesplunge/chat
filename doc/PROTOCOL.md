# Introduction

The protocol for the chat server is made up of a sequence of messages from
client to server, and a second sequence from server to client. These messages
can be sent in either JSON or a custom binary format. Both are outlined below.

# Connection Header

When a connection is first established, the client must inform the server of
which connection mode it intends to use. This is either the JSON message format,
which consists of newline-separated JSON objects that each describe a single
message, or the custom binary format.

    Client -> Server: "JSON\n" | "BINARY\n"

# JSON Messages

Each message sent in the JSON format should contain no extraneous newline
characters. Specifically, the server must be able to delimit the messages purely
by splitting at newline characters.

An example JSON message might be:

    {"type":"IDENTIFY","payload":{"display_name":"Alice"}}\n

All JSON messages must have a "type" field with a string-value that specifies
what message type is being sent, and a "payload" field which contains the data
which is specific to that message type.

# Binary Messages

## Varints

The binary format makes heavy use of `varint`s (variable-length encoded
integers).  A value encoded as a `varint` is encoded using the least-significant
7 bits of each byte to store the value, and the most-significant bit to specify
whether further bytes are required to represent the value. The values are in
little-endian order. This makes it very space-efficient to store small numbers.

Negative numbers which are represented in this manner can become quite large due
to the twos-complement representation as a positive integer, so when it is
necessary to store a signed integer, zigzag encoding is used. This permutes the
map from signed integers to unsigned integers like so:

<table border="1">
  <tr><th>Unsigned Value</th> <th>Signed Value</th></tr>
  <tr><td>0</td>              <td>0</td></tr>
  <tr><td>1</td>              <td>-1</td></tr>
  <tr><td>2</td>              <td>1</td></tr>
  <tr><td>3</td>              <td>-2</td></tr>
  <tr><td>4</td>              <td>2</td></tr>
  <tr><td>5</td>              <td>-3</td></tr>
  <tr><td>6</td>              <td>3</td></tr>
  <tr><td>7</td>              <td>-4</td></tr>
  <tr><td>...</td>            <td>...</td></tr>
</table>

To convert an `int64_t` into a `uint64_t`:

    int64_t value = ...;
    uint64_t encoded = (value << 1) ^ (value >> 63);

To convert a `uint64_t` back into an `int64_t`:

    uint64_t encoded = ...;
    int64_t value = (encoded >> 1) ^ -(encoded & 1);

To store a signed `varint`, simply convert the signed integer into an unsigned
integer using zigzag encoding, and then store as an unsigned `varint`.

From this point onwards, unsigned `varint`s will be referred to as `varuint`s.

[varints]: https://developers.google.com/protocol-buffers/docs/encoding
See [here][varints] for more info.

## Message Format

Each binary message is encoded as follows:

    <varuint type> <varuint length> <bytes[length] payload>

The integer encoding of the types is determined by the following table:

<table border="1">
  <tr><th>Message</th>         <th>ID</th></tr>
  <tr><td>IDENTIFY</td>        <td>1</td></tr>
  <tr><td>SEND_MESSAGE</td>    <td>2</td></tr>
  <tr><td>RECEIVE_MESSAGE</td> <td>3</td></tr>
  <tr><td>REQUEST_HISTORY</td> <td>4</td></tr>
  <tr><td>RECEIVE_HISTORY</td> <td>5</td></tr>
</table>

The payload encoding is specific to each message type, and is described below.

# Messages

## IDENTIFY

Sent from client to server. Sets or changes the display name for this
connection.

### JSON payload format:

    {"display_name":"<string display_name>"}

### Binary payload format:

    <varuint length> <bytes[length] display_name>

## SEND_MESSAGE

Sent from client to server. Sends a chat message.

### JSON payload format:

    {"text":"<string message_body>"}

### Binary payload format:

    <varuint length> <bytes[length] message_body>

## RECEIVE_MESSAGE

Sent from server to client. Contains a single chat message that is being
broadcasted to clients. These can be one of the following types:

    enum Category {
      CHAT_MESSAGE = 0,
      NOTICE = 1,
    };

### JSON payload format:

    {
      "message_id" : <uint64_t message_id>,
      "category" : "<Category category>",
      "sender_name" : "<string sender_name, absent if category is NOTICE>",
      "text" : "<string message_text>"
    }

### Binary payload format:

    <varuint message_id>
    <varuint category>
    <if category is CHAT_MESSAGE>
      <varuint sender_name_length>
      <bytes[sender_name_length] sender_name>
    <endif>
    <varuint text_length>
    <bytes[text_length] text>

## REQUEST_HISTORY

Sent from client to server. Asks the server to send the client a RECEIVE_HISTORY
message containing the requested interval of the message history. The request
consists of `start_id`, which specifies the message ID at which the interval
should start, and `num_messages`, which limits the number of messages returned.

### JSON payload format:

    {"start_id":<uint64_t start_id>,"num_messages":<uint64_t num_messages>}

### Binary payload format:

    <varuint start_id> <varuint num_messages>

## RECEIVE_HISTORY

Sent from server to client in response to a REQUEST_HISTORY message. Contains an
interval of messages from the message history. The messages are each encoded the
same way as the RECEIVE_MESSAGE payload, so this code can be reused.

### JSON payload format:

    [<RECEIVE_MESSAGE PAYLOAD>, <RECEIVE_MESSAGE PAYLOAD>, ...]

### Binary payload format:

    <varuint length>
    <Message<RECEIVE_MESSAGE>[length] messages>

