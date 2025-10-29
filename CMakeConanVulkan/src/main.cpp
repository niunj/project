#include <vsg/all.h>
#include <iostream>

#include <vsg/app/WindowTraits.h>

int main(int argc, char** argv)
{
    try
    {    
        auto instance = vsg::Instance::create();

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}