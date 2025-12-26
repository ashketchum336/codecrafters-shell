#include <iostream>
#include <string>
#include <sstream>
#include <unordered_map>
#include <functional>
#include <cstdlib>
#include <filesystem>
#include <sys/stat.h>
#include <sys/wait.h>
#include <optional>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>

using namespace std;

using BuiltIn = function<void(const vector<string>& args)>;
unordered_map<string, BuiltIn> builtIns;

bool isExecutable(const string& path)
{
  struct stat sb;
  if(stat(path.c_str(), &sb) != 0) return false;
  return sb.st_mode & S_IXUSR;
}

optional<string> findExecutablePath(const string& command)
{
  const char* pathEnv = getenv("PATH");
  if(!pathEnv) return nullopt;

  string pathStr(pathEnv);
  stringstream ss(pathStr);
  string dir;

  while(getline(ss, dir, ':'))
  {
    filesystem::path fullPath = filesystem::path(dir) / command;

    if(filesystem::exists(fullPath) && isExecutable(fullPath.string()))
    {
      return fullPath.string();
    }
  }

  return nullopt;
}

struct ParsedCommand
{
  string name;
  vector<string> args;

  bool redirectStdOut = false;
  string stdOutFile;
};

ParsedCommand parse(const string& input)
{
  ParsedCommand cmd;
  vector<string> args;

  string current;
  bool inSingleQuotes = false;
  bool inDoubleQuotes = false;
  for(int i = 0; i < (int)input.size(); ++i)
  {
    char ch = input[i];
    if(ch == '\\' && !inSingleQuotes && !inDoubleQuotes)
    {
      if(i < (int)input.size() - 1)
      {
        current.push_back(input[i + 1]);
        i++;
      }
    }else if(ch == '\\' && inDoubleQuotes)
    {
      if(i < (int)input.size() - 1)
      {
        char next = input [i + 1];
        if(next == '"' || next == '\\')
        {
          current.push_back(next);
          i++;
        }else
        {
          current.push_back(ch);
        }
      }
    }
    else if(ch == '\'' && !inDoubleQuotes)
    {
      inSingleQuotes = !inSingleQuotes;
    }else if(ch == '"' && !inSingleQuotes)
    {
      inDoubleQuotes = !inDoubleQuotes;
    }
    else if(isspace(ch) && !inSingleQuotes && !inDoubleQuotes)
    {
      if(!current.empty())
      {
        args.push_back(current);
        current.clear();
      }
    }else{
      current.push_back(ch);
    }
  }

  if(!current.empty())
  {
    args.push_back(current);
  }

  vector<string> finalArgs;
  for(int i = 0; i < (int)args.size(); ++i)
  {
    if(args[i] == ">" || args[i] == "1>")
    {
      if(i < (int)args.size() - 1)
      {
        cmd.redirectStdOut = true;
        cmd.stdOutFile = args[i + 1];
        i++;
      }
    }else
    {
      finalArgs.push_back(args[i]);
    }
  }
  
  if(!finalArgs.empty())
  {
    cmd.name = finalArgs[0];
    cmd.args = finalArgs;
  }
  return cmd;
}

void runExternal(const ParsedCommand& cmd)
{
  auto pathOpt = findExecutablePath(cmd.name);
  if (!pathOpt.has_value()) {
    cout << cmd.name << ": command not found" << endl;
    return;
  }

  pid_t pid = fork();

  if (pid == 0) {
    // Child process

    if (cmd.redirectStdOut)
    {
      int fd = open(
        cmd.stdOutFile.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC,
        0644
      );

      if (fd < 0)
      {
        perror("open");
        exit(1);
      }

      dup2(fd, STDOUT_FILENO);
      close(fd);
    }

    vector<char*> argv;
    for (const auto& arg : cmd.args)
      argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    execv(pathOpt->c_str(), argv.data());

    // exec only returns on error
    perror("execv");
    exit(1);
  }
  else if (pid > 0) {
    // Parent process
    int status;
    waitpid(pid, &status, 0);
  }
  else {
    perror("fork");
  }
}

void initBuiltIn()
{
  builtIns["echo"] = [](const vector<string>& args){
      for (size_t i = 1; i < args.size(); ++i) 
      {
        if (i > 1) cout << " ";
        cout << args[i];
      }
      cout << endl;
  };

  builtIns["exit"] = [](const vector<string>&){
    exit(0);
  };

  builtIns["type"] = [](const vector<string>& args){
    if (args.size() < 2) return;

    const string& cmd = args[1];

    if (builtIns.count(cmd)) 
    {
      cout << cmd << " is a shell builtin" << endl;
      return;
    }

    auto executable = findExecutablePath(cmd);
    if (executable.has_value()) 
    {
      cout << cmd << " is " << executable.value() << endl;
    } else {
      cout << cmd << ": not found" << endl;
    }
  };

  builtIns["pwd"] = [](const vector<string>&){
    char cwd[PATH_MAX];
    if(getcwd(cwd, sizeof cwd) != nullptr)
    {
      cout << cwd << endl;
    }else
    {
      perror("getcwd");
    }
  };

  builtIns["cd"] = [](const vector<string>& args){
    if((int)args.size() < 2) return;

    string path = args[1];

    if(path == "~")
    {
      const char* home = getenv("HOME");
      if(!home) return;

      path = home;
    }

    if(chdir(path.c_str()) != 0)
    {
      cout << "cd: " << path << ": No such file or directory" << endl;
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
    }else {
      runExternal(pCmd);
    }
  }
  
}
