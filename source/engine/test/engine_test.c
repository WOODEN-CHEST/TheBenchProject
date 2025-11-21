#include "engine_test.h"
#include <varargs.h>
#include <stdio.h>


// Functions
void ExecuteEngineTest()
{

}

void FormatErrorMessage(TestErrorMessage* msg, const unsigned char* format, ...)
{
    va_list Args;
    va_start(Args, format);
    vsnprintf((char)msg->_message, msg->_maxMessageLength, (const char*)format, Args);
    va_end(Args);
}