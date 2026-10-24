#pragma once
#include <string>

// edscript commands for the RmlUi documents (UI-2):
//   ui-click <selector>                clicks the first matching element (dispatches a click)
//   ui-assert-text <selector> | text   asserts the element's inner RML equals the text
namespace UIAutomation
{
    bool Execute( const std::string& InCommand, const std::string& InArgs, std::string& OutError );
}
