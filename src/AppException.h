#ifndef APP_EXCEPTION_H
#define APP_EXCEPTION_H
/*
** LogicErrors should be eliminated during development
*/
class LogicError : public std::logic_error {
	public:
		LogicError(const String& msg) : std::logic_error(msg.c_str()) {};
};

/*
** RuntimeErrors should occur very rarely and should not be used to control normal programme flow
*/
class RuntimeError : public std::runtime_error {
	public:
		RuntimeError(const String& msg) : std::runtime_error(msg.c_str()) {};
};
#endif