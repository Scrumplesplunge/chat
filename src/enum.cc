/*
  This script takes a file of the form:

  -- Simple.enum --
    A      # Foo
    B(2)   # Bar
    C      # Baz
  -----------------

  And produces a header:

  -- simple.h --
    #include <string>

    namespace Simple {

    enum Simple {
      A,      // Foo
      B = 2,  // Bar
      C,      // Baz
    };

    std::string toString(Simple value);
    bool fromString(std::string value, Simple* output);
  --------------

  And the corresponding implementation. This is because I am tired of repeatedly
  solving the same problem when I want to be able to use the string-names of
  enum values for input/output.
*/

#include <cstdint>
#include <fstream>
#include <iostream>
#include <regex>
#include <scrump/args.h>
#include <scrump/logging.h>
#include <vector>

using namespace scrump;
using namespace std;

USAGE("Usage: enum --input <input file> --name <enum name>\n"
      "            --output <output file>\n"
      "\n"
      "  input file  - Name of the enum file to process.\n"
      "  enum name   - Name to assign to the generated enum."
      "  output file - Name for the generated .h and .cc files, excluding the "
                      "extension.");

OPTION(string, input, "", "Name of the input file.");
OPTION(string, output, "", "Name of the output file.");
OPTION(string, name, "", "Name of the generated enum.");

struct Item {
  string name;
  int64_t value;
  string description;
};

int scrump_main(int argc, char* args[]) {
  // Verify that the output name is a valid enumeration name.
  regex enum_name_syntax(R"([A-Za-z][A-Za-z0-9_]*)");
  if (!regex_match(options::name, enum_name_syntax)) {
    LOG(ERROR) << "Invalid enumeration name '" << options::name << "'.";
    return 1;
  }

  // Open the input file.
  ifstream input(options::input);
  if (!input.good()) {
    LOG(ERROR) << "Failed to open input file '" << options::input << "'.";
    return 1;
  }

  // Generate the enum mapping.
  vector<Item> enumeration;

  int line_number = 0;  // Incremented on each line read, for error messages.
  int64_t index = 0;    // Incremented for each item, reset by explicit values.
  string line;
  // 1 = Enumeration item.
  // 2 = Identifier.
  // 3 = Explicit value parantheses (optional).
  // 4 = Explicit value.
  // 5 = Description (optional).
  // 6 = Description text.
  regex line_syntax(
      //    1
      R"(\s*()"
          // 2                     2
          R"(([A-Za-z][A-Za-z0-9_]*)\s*)"
          // 3     4                               4     3
          R"((\(\s*(0|[1-9][0-9]*|0[xX][0-9A-Fa-f]+)\s*\))?)"
      // 1
      R"()?)"
      //    5    6  65
      R"(\s*(#\s*(.*))?)");
  int max_enum_name_length = 0;  // Longest enumeration item name length.
  int max_enum_id_length = 0;    // Longest decimal representation of an id.
  while (getline(input, line)) {
    line_number++;
    // Match the line to the syntax pattern.
    smatch result;
    if (!regex_match(line, result, line_syntax)) {
      LOG(ERROR) << options::input << ": Syntax error on line " << line_number
                 << "\n  " << line;
      return 1;
    }
    if (result[1].length() != 0) {
      // There is an enumeration item on this line.
      Item item;
      item.name = result[2];

      // If an explicit index was set, use it.
      if (result[3].length() != 0) index = stoll(result[4], 0, 0);
      item.value = index++;

      // If a description was provided, set it.
      if (result[5].length() != 0) item.description = result[6];
      enumeration.push_back(item);

      // Update the maximum length values.
      if (static_cast<int>(item.name.length()) > max_enum_name_length)
        max_enum_name_length = item.name.length();
      int id_length = to_string(item.value).length();
      if (id_length > max_enum_id_length)
        max_enum_id_length = id_length;
    }
  }

  // Construct the header file.
  ofstream output(options::output + ".h");
  if (!output.good()) {
    LOG(ERROR) << "Failed to open header file '" << options::output
               << ".h' for output.";
    return 1;
  }

  output << "// AUTOMATICALLY GENERATED CODE - DO NOT EDIT.\n"
            "\n"
            "#pragma once\n"
            "\n"
            "#include <string>\n"
            "\n"
            "enum " << options::name << " {\n";
  for (auto item : enumeration) {
    output << "  " << item.name
           << string(max_enum_name_length - item.name.length(), ' ')
           << " = " << item.value << ",";
    if (item.description.length() > 0) {
      output << string(max_enum_id_length - to_string(item.value).length(), ' ')
             << "  // " << item.description;
    }
    output << "\n";
  }
  output << "};\n"
            "\n"
            "std::string toString(" << options::name << " value);\n"
            "bool fromString(const std::string& value, "
         << options::name << "* output);\n\n";
  output.close();
  LOG(INFO) << options::output << ".h generated.";

  // Construct the source file.
  output.open(options::output + ".cc");
  if (!output.good()) {
    LOG(ERROR) << "Failed to open source file '" << options::output
               << ".cc' for output.";
    return 1;
  }

  output << "// AUTOMATICALLY GENERATED CODE - DO NOT EDIT.\n"
            "\n"
            "#include \"" << options::output << ".h\"\n"
            "\n"
            "#include <map>\n"
            "\n"
            "using namespace std;\n"
            "\n"
            "string toString(" << options::name << " value) {\n"
            "  switch (value) {\n";
  for (auto item : enumeration) {
    output << "    case " << item.name << ": "
           << string(max_enum_name_length - item.name.length(), ' ')
           << "return \"" << item.name << "\";\n";
  }
  output << "    default: return \"<INVALID>\";\n"
            "  }\n"
            "}\n"
            "\n"
            "bool fromString("
                "const string& value, " << options::name << "* output) {\n"
            "  static const map<string, " << options::name
         << "> lookup = {\n";
  for (auto item : enumeration) {
    output << "    {\"" << item.name << "\", "
           << string(max_enum_name_length - item.name.length(), ' ')
           << item.name << "},\n";
  }
  output << "  };\n"
            "  auto i = lookup.find(value);\n"
            "  if (i == lookup.end()) return false;\n"
            "  *output = i->second;\n"
            "  return true;\n"
            "}\n";
  LOG(INFO) << options::output << ".cc generated.";

  return 0;
}
