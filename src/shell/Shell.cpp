#include <iostream>
#include "Shell.h"

using namespace std;

void Shell::run()
{
    string line;
    while(true)
    {
        cout << "$: ";
        getline(cin, line);

        if(cin.eof())
        {
            cout << endl;
            break;
        }

        execute(line);
    }
}

void Shell::execute(const string& command)
{
    if(command.empty()) return;

    cout << command << ": command not found" << endl;
}