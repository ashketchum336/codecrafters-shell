#pragma once
#include <iostream>
#include <string>

class Shell 
{
    public:
        void run();
    private:
        void execute(const std::string& command);
};

