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
#include <readline/history.h>
#include <readline/readline.h>

using namespace std;

using BuiltIn = function<void(const vector<string>& args)>;
unordered_map<string, BuiltIn> builtIns;

bool isExecutable(const string& path)
{
  struct stat sb;
  if(stat(path.c_str(), &sb) != 0) return false;
  return sb.st_mode & S_IXUSR;
}

vector<string> executablesInPath() {
    vector<string> result;

    const char* pathEnv = getenv("PATH");
    if (!pathEnv) return result;

    string pathStr(pathEnv);
    stringstream ss(pathStr);
    string dir;

    while (getline(ss, dir, ':')) {
        if (dir.empty()) continue;

        // PATH entries may not exist — must handle gracefully
        if (!filesystem::exists(dir) || !filesystem::is_directory(dir))
            continue;

        for (const auto& entry : filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;

            const auto& path = entry.path();
            if (isExecutable(path.string())) {
                result.push_back(path.filename().string());
            }
        }
    }

    return result;
}

char* command_generator(const char* text, int state) {
    static vector<string> matches;
    static size_t index;

    if (state == 0) {
        matches.clear();
        index = 0;

        string prefix(text);

        // 1️⃣ Builtins
        for (const auto& [name, _] : builtIns) {
            if (name == "echo" || name == "exit") {
                if (name.rfind(prefix, 0) == 0) {
                    matches.push_back(name);
                }
            }
        }

        // 2️⃣ External executables
        for (const auto& exe : executablesInPath()) {
            if (exe.rfind(prefix, 0) == 0) {
                matches.push_back(exe);
            }
        }
    }

    if (index < matches.size()) {
        return strdup(matches[index++].c_str());
    }

    return nullptr;
}

char** completion_hook(const char* text, int start, int end) {
    // Only autocomplete the first word
    if (start != 0) return nullptr;

    return rl_completion_matches(text, command_generator);
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

enum class RedirectMode {
  NONE,
  TRUNCATE,  // >
  APPEND     // >>
};

struct FdRedirect {
  RedirectMode mode = RedirectMode::NONE;
  string file;
};

struct ParsedCommand
{
  string name;
  vector<string> args;

  FdRedirect stdoutRedirect;
  FdRedirect stderrRedirect;
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
        cmd.stdoutRedirect = {RedirectMode::TRUNCATE, args[i + 1]};
        i++;
      }
    }else if(args[i] == ">>" || args[i] == "1>>")
    {
      if(i < (int)args.size() - 1)
      {
        cmd.stdoutRedirect = {RedirectMode::APPEND, args[i + 1]};
        i++;
      }
    }
    else if(args[i] == "2>")
    {
      if(i < (int)args.size() - 1)
      {
        cmd.stderrRedirect = {RedirectMode::TRUNCATE, args[i + 1]};
        i++;
      }
    }else if(args[i] == "2>>")
    {
      if(i < (int)args.size() - 1)
      {
        cmd.stderrRedirect = {RedirectMode::APPEND, args[i + 1]};
        i++;
      }
    }
    else
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

int redirectFd(int targetFd, const FdRedirect& r)
{
  if (r.mode == RedirectMode::NONE) return -1;

  int flags = O_WRONLY | O_CREAT;
  if (r.mode == RedirectMode::TRUNCATE)
    flags |= O_TRUNC;
  else
    flags |= O_APPEND;

  int fd = open(r.file.c_str(), flags, 0644);
  if (fd < 0) {
    perror("open");
    return -1;
  }

  int saved = dup(targetFd);
  dup2(fd, targetFd);
  close(fd);
  return saved;
}

void restoreFd(int targetFd, int saved)
{
  if (saved >= 0) {
    dup2(saved, targetFd);
    close(saved);
  }
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

    if (cmd.stdoutRedirect.mode != RedirectMode::NONE)
      redirectFd(STDOUT_FILENO, cmd.stdoutRedirect);

    if (cmd.stderrRedirect.mode != RedirectMode::NONE)
      redirectFd(STDERR_FILENO, cmd.stderrRedirect);

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

struct Pipeline {
  vector<ParsedCommand> cmds;
};

optional<Pipeline> parsePipeline(const string& input) {
  size_t pos = input.find('|');
  if (pos == string::npos) return nullopt;

  string left = input.substr(0, pos);
  string right = input.substr(pos + 1);

  Pipeline p;
  string token;
  stringstream ss(input);

  while (getline(ss, token, '|')) {
    p.cmds.push_back(parse(token));
  }

  return p;
}

void execCommandInChild(const ParsedCommand& cmd) {
  // Builtin
  if (builtIns.count(cmd.name)) {
    builtIns[cmd.name](cmd.args);
    exit(0);
  }

  // External
  auto path = findExecutablePath(cmd.name);
  if (!path) {
    cerr << cmd.name << ": command not found\n";
    exit(1);
  }

  vector<char*> argv;
  for (auto& a : cmd.args)
    argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);

  execv(path->c_str(), argv.data());
  perror("execv");
  exit(1);
}

void runPipeline(const vector<ParsedCommand>& cmds) {
  int n = cmds.size();
  if (n < 2) return;

  vector<array<int, 2>> pipes(n - 1);

  // Create pipes
  for (int i = 0; i < n - 1; ++i) {
    if (pipe(pipes[i].data()) < 0) {
      perror("pipe");
      return;
    }
  }

  vector<pid_t> pids;

  for (int i = 0; i < n; ++i) {
    pid_t pid = fork();
    if (pid == 0) {
      // stdin
      if (i > 0) {
        dup2(pipes[i - 1][0], STDIN_FILENO);
      }

      // stdout
      if (i < n - 1) {
        dup2(pipes[i][1], STDOUT_FILENO);
      }

      // Close ALL pipe fds in child
      for (auto& p : pipes) {
        close(p[0]);
        close(p[1]);
      }

      execCommandInChild(cmds[i]);
    }

    pids.push_back(pid);
  }

  // Parent closes all pipe fds
  for (auto& p : pipes) {
    close(p[0]);
    close(p[1]);
  }

  // Wait for all children
  for (pid_t pid : pids) {
    waitpid(pid, nullptr, 0);
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

  builtIns["history"] = [](const vector<string>& args) {
    // history -r <file>
    if (args.size() == 3 && args[1] == "-r") {
        if (read_history(args[2].c_str()) != 0) {
            perror("history");
        }
        return;
    }

    // history -w <file>
    if (args.size() == 3 && args[1] == "-w") {
        if (write_history(args[2].c_str()) != 0) {
            perror("history");
        }
        return;
    }

    HIST_ENTRY** list = history_list();
    if (!list) return;

    int total = history_length;
    int n = total;

    // history <n>
    if (args.size() == 2) {
        try {
            n = stoi(args[1]);
            if (n < 0) n = 0;
        } catch (...) {
            return;
        }
    }

    int start = max(0, total - n);
    for (int i = start; i < total; ++i) {
        cout << "    " << (i + 1) << "  " << list[i]->line << endl;
    }
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
  rl_attempted_completion_function = completion_hook;

  while(true)
  {
    char* line = readline("$ ");
    if (!line) {
      cout << endl;
      break;
    }

    string input(line);
    free(line);

    if (!input.empty()) {
      add_history(input.c_str());
    }

    // 1️⃣ First: check for pipeline
    auto pipeline = parsePipeline(input);
    if (pipeline) {
      runPipeline(pipeline->cmds);
      continue;
    }

    // 2️⃣ Otherwise: normal command execution
    ParsedCommand pCmd = parse(input);
    if(builtIns.count(pCmd.name))
    {
      int savedOut = redirectFd(STDOUT_FILENO, pCmd.stdoutRedirect);
      int savedErr = redirectFd(STDERR_FILENO, pCmd.stderrRedirect);

      builtIns[pCmd.name](pCmd.args);

      restoreFd(STDOUT_FILENO, savedOut);
      restoreFd(STDERR_FILENO, savedErr);
    }else {
      runExternal(pCmd);
    }
  }
  
}
