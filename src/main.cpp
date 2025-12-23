#include <iostream>
#include <string>
#include <sstream>
#include <unordered_map>
using namespace std;

using BuiltIn = function<void(const string& args)>;
unordered_map<string, BuiltIn> builtIns;

struct ParsedCommand
{
  string name;
  string args;
};

ParsedCommand parse(const string& input)
{
  istringstream iss(input);
  ParsedCommand cmd;
  iss >> cmd.name;
  iss >> std::ws;
  getline(iss, cmd.args);
  return cmd;
}

void initBuiltIn()
{
  builtIns["echo"] = [](const string& args){
      cout << args << endl;
  };

  builtIns["exit"] = [](const string&){
    exit(0);
  };

  builtIns["type"] = [](const string& args){
    if(builtIns.count(args))
    {
      cout << args << " is a shell builtin" << endl;
    }else
    {
      cout << args << ": not found" << endl;
    }
  };
}

int main() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  initBuiltIn();

  while(true)
  {
    cout << "$ ";
    string input;
    if(!getline(cin, input))
    {
      cout << endl;
      break;
    }

    ParsedCommand pCmd = parse(input);
    if(builtIns.count(pCmd.name))
    {
      builtIns[pCmd.name](pCmd.args);
    }else
    {
      cout << pCmd.name << ": command not found" << endl;
    }
  }
  
}
