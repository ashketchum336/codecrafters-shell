#include <iostream>
#include <string>
#include <sstream>

using namespace std;

int main() {
  // Flush after every std::cout / std:cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  while(true)
  {
    cout << "$ ";
    string input;
    if(!getline(cin, input))
    {
      cout << endl;
      break;
    }

    if(input == "exit")
    {
      exit(0);
    }

    istringstream iss(input);
    string command;
    iss >> command;

    if(command == "echo")
    {
      iss >> std::ws;
      string rest;
      getline(iss, rest);
      cout << rest << endl;
      continue;
    }
      
    
    cout << input << ": command not found" << endl;
  }
  
}
